/* =====================================================================
   analysis.cpp  —  Burst capture + FFT learn + Goertzel verify
   ---------------------------------------------------------------------
   Sampling: the ESP8266 can't stream fast ADC continuously alongside
   WiFi, but short paced bursts are fine. We pace analogRead at 125us
   (~8kHz), measure the *actual* achieved rate with micros(), and use
   that measured rate in all frequency math — so even if analogRead is
   slower than hoped, learn and detect stay mutually consistent.

   Learning burst: 1024 samples (~128ms) -> Hann window -> radix-2 FFT
   -> top spectral peak with parabolic interpolation, plus a secondary
   peak if one stands out. Stored into settings (persisted).

   Detect burst: 256 samples (~32ms) -> Goertzel power at f1 (and f2),
   each evaluated at f-df/f/f+df to tolerate slight detuning. Pass if
   tone energy fraction >= settings.toneRatio.
   ===================================================================== */
#include "analysis.h"
#include "config.h"
#include "settings.h"

#define AN_LEARN_N   1024
#define AN_DETECT_N   256
#define AN_PACE_US    125      // target ~8kHz
#define AN_MIN_HZ     150.0f   // ignore rumble/DC below this in peak search

static bool        g_armed      = false;
static uint32_t    g_armedUntil = 0;
static LearnResult g_learn      = {false, 0, 0, 0, 0};

// Static DSP buffers (avoid heap churn). ~10KB total.
static uint16_t s_cap[AN_LEARN_N];
static float    s_re[AN_LEARN_N];
static float    s_im[AN_LEARN_N];

void analysisLearnArm()  { g_armed = true; g_armedUntil = millis() + 60000UL; }
bool analysisLearnArmed(){
  if (g_armed && (int32_t)(millis() - g_armedUntil) >= 0) g_armed = false;
  return g_armed;
}
LearnResult analysisGetLearn() { return g_learn; }

// ---- paced capture; returns measured sample rate (Hz) ----
static float captureBurst(uint16_t* dst, int n) {
  uint32_t t0 = micros();
  uint32_t next = t0;
  for (int i = 0; i < n; i++) {
    while ((int32_t)(micros() - next) < 0) { /* spin */ }
    dst[i] = analogRead(PIN_MIC_AO);
    next += AN_PACE_US;
    if ((i & 127) == 127) ESP.wdtFeed();
  }
  uint32_t t1 = micros();
  return (float)n * 1e6f / (float)(t1 - t0);
}

// ---- in-place radix-2 FFT ----
static void fftRadix2(float* re, float* im, int n) {
  for (int i = 1, j = 0; i < n; i++) {          // bit-reversal permutation
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j |= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    float ang = -2.0f * PI / (float)len;
    float wr = cosf(ang), wi = sinf(ang);
    for (int i = 0; i < n; i += len) {
      float cr = 1, ci = 0;
      for (int k = 0; k < len / 2; k++) {
        int a = i + k, b = i + k + len / 2;
        float xr = re[b] * cr - im[b] * ci;
        float xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi;
        re[a] += xr;        im[a] += xi;
        float ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr; cr = ncr;
      }
    }
    ESP.wdtFeed();
  }
}

void analysisCaptureLearn() {
  g_armed = false;
  float sr = captureBurst(s_cap, AN_LEARN_N);

  // De-mean + Hann window into the FFT buffers.
  float mean = 0;
  for (int i = 0; i < AN_LEARN_N; i++) mean += s_cap[i];
  mean /= AN_LEARN_N;
  for (int i = 0; i < AN_LEARN_N; i++) {
    float wnd = 0.5f - 0.5f * cosf(2.0f * PI * i / (AN_LEARN_N - 1));
    s_re[i] = ((float)s_cap[i] - mean) * wnd;
    s_im[i] = 0;
  }
  fftRadix2(s_re, s_im, AN_LEARN_N);

  // Magnitude spectrum reused in s_re; search above AN_MIN_HZ.
  int half = AN_LEARN_N / 2;
  int minBin = (int)(AN_MIN_HZ * AN_LEARN_N / sr);
  if (minBin < 2) minBin = 2;
  for (int i = 0; i < half; i++) s_re[i] = s_re[i] * s_re[i] + s_im[i] * s_im[i];

  auto peakAt = [&](int lo, int hi, int skipC, int skipW) -> int {
    int best = -1; float bm = 0;
    for (int i = lo; i < hi; i++) {
      if (skipC >= 0 && i >= skipC - skipW && i <= skipC + skipW) continue;
      if (s_re[i] > bm) { bm = s_re[i]; best = i; }
    }
    return best;
  };
  auto refine = [&](int i) -> float {   // parabolic interpolation
    if (i <= 0 || i >= half - 1) return (float)i;
    float a = s_re[i - 1], b = s_re[i], c = s_re[i + 1];
    float d = a - 2 * b + c;
    return (fabsf(d) < 1e-9f) ? (float)i : (float)i + 0.5f * (a - c) / d;
  };

  int p1 = peakAt(minBin, half, -1, 0);
  if (p1 < 0) { g_learn.valid = false; return; }
  int p2 = peakAt(minBin, half, p1, 4);

  g_learn.sr    = sr;
  g_learn.f1    = refine(p1) * sr / AN_LEARN_N;
  g_learn.mag2rel = (p2 >= 0) ? s_re[p2] / s_re[p1] : 0;
  // 0.03 in POWER terms ~= 17% amplitude — harmonics are often much
  // weaker than the fundamental; a power gate of 0.10 (31% amplitude)
  // rejected real secondary partials (caught by host test).
  g_learn.f2    = (p2 >= 0 && g_learn.mag2rel >= 0.03f)
                  ? refine(p2) * sr / AN_LEARN_N : 0;
  g_learn.valid = true;

  settings.toneF1 = g_learn.f1;
  settings.toneF2 = g_learn.f2;
  saveSettings();
  Serial.printf("[LEARN] sr=%.0fHz f1=%.1fHz f2=%.1fHz (rel %.2f)\n",
                sr, g_learn.f1, g_learn.f2, g_learn.mag2rel);
}

// ---- Goertzel power at frequency f over n de-meaned samples ----
static float goertzel(const uint16_t* x, int n, float mean, float sr, float f) {
  float w  = 2.0f * PI * f / sr;
  float cw = 2.0f * cosf(w);
  float s1 = 0, s2 = 0;
  for (int i = 0; i < n; i++) {
    float s0 = ((float)x[i] - mean) + cw * s1 - s2;
    s2 = s1; s1 = s0;
  }
  return s1 * s1 + s2 * s2 - cw * s1 * s2;
}

bool analysisVerifyTone() {
  if (settings.toneF1 < AN_MIN_HZ) return true;   // nothing learned yet

  float sr = captureBurst(s_cap, AN_DETECT_N);
  float mean = 0, etot = 0;
  for (int i = 0; i < AN_DETECT_N; i++) mean += s_cap[i];
  mean /= AN_DETECT_N;
  for (int i = 0; i < AN_DETECT_N; i++) {
    float d = (float)s_cap[i] - mean;
    etot += d * d;
  }
  if (etot < 1.0f) return false;                  // silence: not a chime

  // Evaluate at f-df / f / f+df to tolerate slight detuning; take max.
  float df = sr / AN_DETECT_N;
  auto bandPower = [&](float f) -> float {
    if (f < AN_MIN_HZ || f > sr * 0.45f) return 0;
    float a = goertzel(s_cap, AN_DETECT_N, mean, sr, f - df);
    float b = goertzel(s_cap, AN_DETECT_N, mean, sr, f);
    float c = goertzel(s_cap, AN_DETECT_N, mean, sr, f + df);
    float m = a > b ? a : b; if (c > m) m = c;
    return m;
  };
  float p = bandPower(settings.toneF1);
  if (settings.toneF2 >= AN_MIN_HZ) p += bandPower(settings.toneF2);

  // Fraction of total energy in the learned tone(s). A full-scale pure
  // tone gives fraction ~1.0 with this normalization.
  float frac = p / (etot * (float)AN_DETECT_N * 0.5f);
  if (frac > 1.0f) frac = 1.0f;
  bool pass = frac >= settings.toneRatio;
  Serial.printf("[TONE] frac=%.3f (need %.2f) -> %s\n",
                frac, settings.toneRatio, pass ? "CHIME" : "reject");
  return pass;
}
