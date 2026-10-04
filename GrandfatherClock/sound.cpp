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
// Fast envelope follower. (2.16.0 tried a peak-hold envelope instead: in
// simulation it gave steadier chime peaks, but on the real device chimes
// stopped registering and the quiet trace changed character, so 2.16.2
// went back to this proven EMA. Re-test on hardware before trying again.)
static const float A_ENV     = 0.25f;
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

// Pending chime: a strike has triggered and we are still measuring its peak.
static bool     g_pending      = false;
static uint32_t g_pendingOnset = 0;
static float    g_pendingPeak  = 0.0f;

// gain-calibration rolling window accumulators
static uint16_t g_rawMinAcc   = 1023;
static uint16_t g_rawMaxAcc   = 0;
static uint16_t g_clipAcc     = 0;
static uint32_t g_rawWinStart = 0;

// sampling telemetry window accumulators
static uint16_t g_sampleCntAcc = 0;
static uint32_t g_gapMaxAccUs  = 0;
static uint32_t g_rateWinStart = 0;

static SoundState g_state = {0, 0, 0, 0, 0, 0, 1023, 0, 0, 0, 0};

// --- coarse history: one peak per second, survives browser reloads ---
// An entry closes every HIST_BINS scope bins rather than on its own millis()
// timer, so entry boundaries line up exactly with scope bins and the page can
// fill holes in its 25ms trace from here.
static uint16_t g_hist[HIST_SECONDS] = {0};
static uint32_t g_histSeq    = 0;    // total entries ever written
static float    g_histMax    = 0.0f;
static uint16_t g_histBins   = 0;    // bins folded into the open entry
static uint32_t g_histEndBin = 0;    // g_ringSeq when the newest entry closed

// --- scope bins: 25ms peak-holds of `excess`, ring-buffered ---
// Stored like g_hist (excess * HIST_SCALE as uint16): half the DRAM of floats.
static uint16_t g_ring[SCOPE_RING] = {0};
static uint32_t g_ringSeq   = 0;      // total bins ever written
static float    g_binMax    = 0.0f;
static uint32_t g_binStart  = 0;

// excess (0..1) -> uint16 at HIST_SCALE resolution, rounded and clamped.
static uint16_t toScaled(float v) {
  v = v * (float)HIST_SCALE + 0.5f;
  if (v < 0) v = 0;
  if (v > 65535.0f) v = 65535.0f;
  return (uint16_t)v;
}

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

// Completed chimes wait here until delivered to the callback. soundUpdate()
// delivers at once; soundSampleOnly() (used inside HTTP handlers that stream
// log files) leaves them queued so the callback's file writes and log
// rotation never run underneath an open log being streamed.
#define CHIME_QUEUE 16
static float    g_qPeak[CHIME_QUEUE];
static uint32_t g_qOnsetMs[CHIME_QUEUE];
static uint8_t  g_qN = 0;

static void sampleIfDue() {
  uint32_t now = micros();
  // Handle micros() wrap safely with unsigned subtraction.
  uint32_t gapUs = now - g_lastSampleUs;
  if (gapUs < (uint32_t)SAMPLE_INTERVAL_US) return;
  g_lastSampleUs = now;

  int raw = analogRead(PIN_MIC_AO);
  float x = raw / 1023.0f;

  // Sampling telemetry: reads actually achieved per second and the longest
  // stretch without one. Anything that blocks loop() (OLED push, HTTP, flash
  // write) shows up here as a gap the detector was deaf for.
  g_sampleCntAcc++;
  if (gapUs > g_gapMaxAccUs) g_gapMaxAccUs = gapUs;
  uint32_t rms = millis();
  if ((uint32_t)(rms - g_rateWinStart) >= 1000) {
    g_state.sampleRate = (uint16_t)((uint32_t)g_sampleCntAcc * 1000UL / (rms - g_rateWinStart));
    g_state.maxGapUs   = g_gapMaxAccUs;
    g_sampleCntAcc = 0; g_gapMaxAccUs = 0;
    g_rateWinStart = rms;
  }

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
    g_ring[g_ringSeq % SCOPE_RING] = toScaled(g_binMax);
    g_ringSeq++;

    // Coarse history: every HIST_BINS bins make one ~1-second entry.
    if (g_binMax > g_histMax) g_histMax = g_binMax;
    if (++g_histBins >= HIST_BINS) {
      g_hist[g_histSeq % HIST_SECONDS] = toScaled(g_histMax);
      g_histSeq++;
      g_histMax    = 0.0f;
      g_histBins   = 0;
      g_histEndBin = g_ringSeq;
    }

    g_binMax = 0.0f;
    // Fixed cadence so bins average exactly SCOPE_BIN_MS and HIST_BINS of
    // them are one second. (binStart = now made every bin ~26ms, because
    // samples land 2ms apart.) After a long stall, resync instead of
    // emitting a burst of catch-up bins.
    g_binStart += SCOPE_BIN_MS;
    if ((uint32_t)(ms0 - g_binStart) >= 1000) g_binStart = ms0;
  }

  // Publish live values.
  g_state.level   = g_env;
  g_state.ambient = g_ambient;
  g_state.peak    = g_peakHold;

  // Chime decision: above threshold and past the refractory window.
  uint32_t ms = millis();
  bool past = (uint32_t)(ms - g_lastChimeMs) >= settings.refractoryMs;
  if (excess >= settings.threshold && past && !g_pending) {
    g_lastChimeMs = ms;                     // consume the trigger either way
    if (analysisLearnArmed()) {
      analysisCaptureLearn();               // learning consumes this strike
    } else if (!settings.toneEnabled || analysisVerifyTone()) {
      // Don't report yet — a chime's envelope peaks well after it crosses
      // the threshold. Start a measurement window and report the maximum.
      g_pending      = true;
      g_pendingOnset = ms;
      g_pendingPeak  = excess;
    }
  }

  // While a chime is pending, track its true peak, then report it.
  if (g_pending) {
    if (excess > g_pendingPeak) g_pendingPeak = excess;
    if ((uint32_t)(ms - g_pendingOnset) >= CHIME_PEAK_WINDOW_MS) {
      g_pending = false;
      g_state.chimeCount++;
      g_state.lastChimePeak = g_pendingPeak;
      if (g_qN < CHIME_QUEUE) {
        g_qPeak[g_qN] = g_pendingPeak; g_qOnsetMs[g_qN] = g_pendingOnset; g_qN++;
      }
    }
  }
}

void soundUpdate() {
  sampleIfDue();
  // Onset age is measured at delivery, so a chime that waited in the queue
  // is still timestamped at its true onset.
  for (uint8_t i = 0; i < g_qN; i++)
    if (g_cb) g_cb(g_qPeak[i], (uint32_t)(millis() - g_qOnsetMs[i]));
  g_qN = 0;
}

void soundSampleOnly() { sampleIfDue(); }

SoundState soundGetState() { return g_state; }

uint32_t soundScopeSeq() { return g_ringSeq; }
float soundScopeAt(uint32_t bin) {
  return g_ring[bin % SCOPE_RING] / (float)HIST_SCALE;
}

void soundNoteChimeEpoch(uint32_t epoch) { g_state.lastChimeEpoch = epoch; }

uint16_t soundHistoryCount() {
  return (g_histSeq < HIST_SECONDS) ? (uint16_t)g_histSeq : (uint16_t)HIST_SECONDS;
}
uint32_t soundHistorySeq() { return g_histSeq; }
uint32_t soundHistoryEndBin() { return g_histEndBin; }
uint16_t soundHistoryAtSeq(uint32_t k) {
  if (k >= g_histSeq || g_histSeq - k > HIST_SECONDS) return 0;   // not retained
  return g_hist[k % HIST_SECONDS];
}

uint16_t soundGetHistory(uint16_t* out, uint16_t maxN, uint32_t* outSeq) {
  *outSeq = g_histSeq;
  uint16_t avail = (g_histSeq < HIST_SECONDS) ? (uint16_t)g_histSeq : HIST_SECONDS;
  if (avail > maxN) avail = maxN;
  // oldest-first
  for (uint16_t i = 0; i < avail; i++)
    out[i] = g_hist[(g_histSeq - avail + i) % HIST_SECONDS];
  return avail;
}
