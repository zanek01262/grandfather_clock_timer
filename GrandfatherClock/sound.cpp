/* =====================================================================
   sound.cpp  —  LM393 analog chime detection
   ---------------------------------------------------------------------
   Pipeline (all fixed-point-free, runs comfortably on the ESP8266):

     A0 raw (0..1023)
       -> normalize to 0..1
       -> rectify around a running DC center (|x - center|)
       -> envelope = fast EMA of the rectified signal
       -> ambient  = slow EMA of the envelope (the "quiet floor")
       -> excess   = envelope - ambient
       -> if excess > threshold AND outside refractory window -> CHIME

   center  tracks DC bias so we don't care whether the mic sits at
           ~0.5 (5V-ish) or lower (3V3) — it self-zeroes.
   peak    is a decaying peak-hold of `excess`, purely for the UI meter.

   Sampling is paced to ~500 Hz via SAMPLE_INTERVAL_US; soundUpdate()
   returns immediately if it's not yet time, so loop() stays responsive.
   ===================================================================== */
#include "sound.h"
#include "config.h"
#include "settings.h"
#include "analysis.h"

// --- EMA smoothing factors (per-sample) ---
static const float A_CENTER  = 0.0008f;  // very slow: DC bias tracking
static const float A_ENV     = 0.25f;    // fast: envelope follower
// Ambient floor tracking is ASYMMETRIC and that is deliberate. A symmetric
// EMA let a loud chime drag the "quiet floor" up with it, so for ~3.2s after
// every strike `excess` clamped to zero and the live plot went dead — the
// "cooling off period". A noise floor should follow the QUIET level: rise
// slowly (transients barely move it), fall quickly (adapt to a quieter room).
static const float A_AMB_UP   = 0.00008f; // ~25s: chimes barely lift the floor
static const float A_AMB_DOWN = 0.004f;   // ~0.5s: recovers promptly
static const float PEAK_DECAY = 0.997f;  // per-sample; ~460ms half-life so a 5Hz UI can see it

static ChimeCallback g_cb = nullptr;

static float    g_center   = 0.5f;
static float    g_env      = 0.0f;
static float    g_ambient  = 0.0f;
static float    g_peakHold = 0.0f;

static uint32_t g_lastSampleUs = 0;
static uint32_t g_lastChimeMs  = 0;

// gain-calibration rolling window accumulators
static uint16_t g_rawMinAcc   = 1023;
static uint16_t g_rawMaxAcc   = 0;
static uint16_t g_clipAcc     = 0;
static uint32_t g_rawWinStart = 0;

static SoundState g_state = {0, 0, 0, 0, 0, 0, 1023, 0, 0};

// --- scope bins: 25ms peak-holds of `excess`, ring-buffered ---
static float    g_ring[SCOPE_RING] = {0};
static uint32_t g_ringSeq   = 0;      // total bins ever written
static float    g_binMax    = 0.0f;
static uint32_t g_binStart  = 0;

void soundBegin(ChimeCallback cb) {
  g_cb = cb;
  pinMode(PIN_MIC_AO, INPUT);

  // Seed center from a few reads so the first envelope isn't a spike.
  float acc = 0;
  for (int i = 0; i < 32; i++) { acc += analogRead(PIN_MIC_AO) / 1023.0f; delay(1); }
  g_center  = acc / 32.0f;
  g_env     = 0.0f;
  g_ambient = 0.0f;
  g_lastSampleUs = micros();
}

void soundUpdate() {
  uint32_t now = micros();
  // Handle micros() wrap safely with unsigned subtraction.
  if ((uint32_t)(now - g_lastSampleUs) < (uint32_t)SAMPLE_INTERVAL_US) return;
  g_lastSampleUs = now;

  int raw = analogRead(PIN_MIC_AO);
  float x = raw / 1023.0f;

  // Gain-calibration telemetry in the RAW ADC domain. Track the min/max
  // swing over a ~500ms rolling window and count near-rail (clipping)
  // samples. This is what the mic-gain tool reads to guide the pot.
  if (raw < g_rawMinAcc) g_rawMinAcc = raw;
  if (raw > g_rawMaxAcc) g_rawMaxAcc = raw;
  if (raw <= 3 || raw >= 1020) g_clipAcc++;
  if ((uint32_t)(millis() - g_rawWinStart) >= 500) {
    g_state.rawMin    = g_rawMinAcc;
    g_state.rawMax    = g_rawMaxAcc;
    g_state.clipCount = g_clipAcc;
    g_rawMinAcc = 1023; g_rawMaxAcc = 0; g_clipAcc = 0;
    g_rawWinStart = millis();
  }

  // Track DC center slowly, rectify around it.
  g_center += A_CENTER * (x - g_center);
  float rect = fabsf(x - g_center);

  // Envelope follower (fast), ambient floor (slow).
  g_env     += A_ENV     * (rect  - g_env);
  g_ambient += ((g_env > g_ambient) ? A_AMB_UP : A_AMB_DOWN) * (g_env - g_ambient);

  float excess = g_env - g_ambient;
  if (excess < 0) excess = 0;

  // Decaying peak-hold for the meter.
  g_peakHold *= PEAK_DECAY;
  if (excess > g_peakHold) g_peakHold = excess;

  // Scope bin accumulation: keep the max excess seen in each 25ms window.
  if (excess > g_binMax) g_binMax = excess;
  uint32_t ms0 = millis();
  if ((uint32_t)(ms0 - g_binStart) >= (uint32_t)SCOPE_BIN_MS) {
    g_ring[g_ringSeq % SCOPE_RING] = g_binMax;
    g_ringSeq++;
    g_binMax   = 0.0f;
    g_binStart = ms0;
  }

  // Publish live values.
  g_state.level   = g_env;
  g_state.ambient = g_ambient;
  g_state.peak    = g_peakHold;

  // Chime decision: above threshold and past the refractory window.
  uint32_t ms = millis();
  bool past = (uint32_t)(ms - g_lastChimeMs) >= settings.refractoryMs;
  if (excess >= settings.threshold && past) {
    g_lastChimeMs = ms;                     // consume the trigger either way
    if (analysisLearnArmed()) {
      analysisCaptureLearn();               // learning consumes this strike
    } else if (!settings.toneEnabled || analysisVerifyTone()) {
      g_state.chimeCount++;
      g_state.lastChimePeak = excess;
      if (g_cb) g_cb(excess);
    }
  }
}

SoundState soundGetState() { return g_state; }

void soundGetScope(float* out, uint8_t n, uint32_t* seq) {
  if (n > SCOPE_RING) n = SCOPE_RING;
  *seq = g_ringSeq;
  for (uint8_t i = 0; i < n; i++) {
    // oldest-first: bin (seq - n + i)
    uint32_t idx = g_ringSeq - n + i;
    out[i] = (g_ringSeq >= n) ? g_ring[idx % SCOPE_RING] : 0.0f;
  }
}

void soundNoteChimeEpoch(uint32_t epoch) { g_state.lastChimeEpoch = epoch; }
