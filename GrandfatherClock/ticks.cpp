/* =====================================================================
   ticks.cpp  —  Escapement tick capture + beat analysis
   ---------------------------------------------------------------------
   analyzeBeats() is pure math (host-tested). The capture path samples A0
   at ~TICK_SR Hz for TICK_WINDOW_S seconds, detecting onsets as sharp
   rises of a fast envelope above a slow floor, with a refractory guard
   so one tick isn't counted twice. Only onset *timestamps* are stored.

   Rate math:
     A healthy escapement alternates tick->tock->tick... The full cycle
     (two beats) should take 2*nominalBeat. If the movement actually
     keeps period P_full/2 = measuredBeat, then it gains/loses
        rate = (nominalBeat - measuredBeat)/nominalBeat * 86400  [s/day]
     (measuredBeat SHORTER than nominal => more beats/day => FAST => +).

   Beat error:
     Intervals alternate A,B,A,B... (tick->tock = A, tock->tick = B).
     A perfectly in-beat clock has A==B. Beat error (the timegrapher
     number) is half the mean |A-B|, in ms.
   ===================================================================== */
#include "ticks.h"
#include "config.h"
#include "settings.h"

// ---------------- pure analysis ----------------
BeatResult analyzeBeats(const float* onsets, int n, float nominalBeat) {
  BeatResult r = {false, 0, 0, 0, 0, 0};
  if (n < 4) { r.nOnsets = (uint8_t)(n < 0 ? 0 : n); return r; }
  r.nOnsets = (uint8_t)(n > 255 ? 255 : n);

  // Intervals between consecutive onsets.
  int m = n - 1;
  // median interval for auto-nominal + outlier rejection
  float tmp[TICK_MAX_ONSETS];
  for (int i = 0; i < m && i < TICK_MAX_ONSETS; i++) tmp[i] = onsets[i + 1] - onsets[i];
  int mm = m < TICK_MAX_ONSETS ? m : TICK_MAX_ONSETS;
  // simple insertion sort (small n)
  for (int i = 1; i < mm; i++) {
    float v = tmp[i]; int j = i - 1;
    while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = v;
  }
  float median = tmp[mm / 2];
  if (median <= 0) return r;

  // Alternating intervals: tick->tock (class A) and tock->tick (class B)
  // strictly alternate. We track parity but RESYNC it on any interval that
  // fails the median gate (a missed/spurious onset), so one glitch corrupts
  // only its local neighbours instead of flipping every later interval.
  double sumA = 0, sumB = 0; int nA = 0, nB = 0;
  int parity = 0;
  for (int i = 0; i < m; i++) {
    float iv = onsets[i + 1] - onsets[i];
    if (iv < 0.78f * median || iv > 1.28f * median) {
      parity = -1;            // desynced: skip this and re-anchor next good one
      continue;
    }
    if (parity < 0) { parity = (i & 1); }   // re-anchor to current index parity
    if (parity == 0) { sumA += iv; nA++; } else { sumB += iv; nB++; }
    parity ^= 1;
  }
  if (nA + nB < 3 || nA == 0 || nB == 0) return r;

  // Capture-quality gate: if a meaningful fraction of intervals fell
  // outside the median window, onsets were missed or spurious and the
  // beat structure can't be trusted — mark invalid so the caller re-runs.
  int rejected = m - (nA + nB);
  if (rejected > m / 8 + 1) { r.nOnsets = (uint8_t)n; return r; }

  float meanA = (float)(sumA / nA);
  float meanB = (float)(sumB / nB);
  // Symmetric mean of the two alternating classes: unbiased by A/B count
  // imbalance (odd interval counts) and by starting phase.
  float meanBeat = 0.5f * (meanA + meanB);

  float nominal = (nominalBeat > 0) ? nominalBeat : meanBeat;
  r.beatPeriod    = meanBeat;
  r.rateSecPerDay = (nominal - meanBeat) / nominal * 86400.0f;
  r.beatErrorMs   = fabsf(meanA - meanB) * 0.5f * 1000.0f;
  r.valid = true;
  return r;
}

// ---------------- hardware capture ----------------
#ifndef HOST_TEST   // the sampling path is compiled only on-device

static bool       g_armed = false;
static BeatResult g_last  = {false, 0, 0, 0, 0, 0};
static float      g_nominal = 0;   // 0 => auto

void  ticksArm()        { g_armed = true; }
bool  ticksArmed()      { return g_armed; }
BeatResult ticksGetLast(){ return g_last; }
void  ticksSetNominal(float b) { g_nominal = b; }
float ticksGetNominal() { return g_nominal; }

void ticksCapture() {
  g_armed = false;

  const uint32_t paceUs = 1000000UL / TICK_SR;
  float onsets[TICK_MAX_ONSETS];
  int   nOn = 0;
  float ampAccum = 0;

  // envelope followers
  float center = analogRead(PIN_MIC_AO) / 1023.0f;
  float env = 0, floorv = 0;
  uint32_t t0 = micros();
  uint32_t next = t0;
  uint32_t lastOnsetUs = 0;
  const uint32_t windowUs = (uint32_t)(TICK_WINDOW_S * 1000000.0f);
  const uint32_t refractoryUs = (uint32_t)(TICK_REFRACTORY_MS * 1000UL);

  while ((uint32_t)(micros() - t0) < windowUs && nOn < TICK_MAX_ONSETS) {
    // Wait for the next sample time. Bounded spin (not "< 0" which can
    // livelock across a micros() wrap): compute remaining time as a signed
    // delta and break out if we've already passed the target.
    for (;;) {
      int32_t rem = (int32_t)(next - micros());
      if (rem <= 0) break;
      if (rem > 2000) { yield(); }   // long wait: let WiFi/loop breathe
    }
    next += paceUs;

    float x = analogRead(PIN_MIC_AO) / 1023.0f;
    center += 0.0006f * (x - center);
    float rect = fabsf(x - center);
    env    += 0.30f  * (rect - env);      // fast envelope
    floorv += 0.002f * (env  - floorv);   // slow floor
    float excess = env - floorv;
    if (excess < 0) excess = 0;

    uint32_t nowUs = micros();
    // onset = excess crosses a dynamic threshold, past refractory
    if (excess > floorv * TICK_ONSET_K + TICK_ONSET_MIN &&
        (lastOnsetUs == 0 || (uint32_t)(nowUs - lastOnsetUs) > refractoryUs)) {
      onsets[nOn++] = (float)(nowUs - t0) / 1000000.0f;
      ampAccum += excess;
      lastOnsetUs = nowUs;
    }
    if ((nOn & 7) == 0) ESP.wdtFeed();
    yield();   // keep WiFi/loop alive between samples
  }

  g_last = analyzeBeats(onsets, nOn, g_nominal > 0 ? g_nominal : 0);
  if (g_last.valid && nOn > 0) g_last.amplitudeRel = ampAccum / nOn;
  Serial.printf("[TICK] onsets=%d beat=%.4fs rate=%+.1fs/day beatErr=%.1fms\n",
                nOn, g_last.beatPeriod, g_last.rateSecPerDay, g_last.beatErrorMs);
}


// ---------------- tick sensitivity diagnostics ----------------
// Same front end as ticksCapture(), but reports what the mic is actually
// hearing rather than a rate. Used by the dashboard's sensitivity tool.
TickDiag ticksDiagnose(float windowSeconds) {
  TickDiag d = {0, windowSeconds, 0, 0, 0, 0, 0};
  if (windowSeconds < 2.0f)  windowSeconds = 2.0f;
  if (windowSeconds > 20.0f) windowSeconds = 20.0f;
  d.windowS = windowSeconds;

  const uint32_t paceUs = 1000000UL / TICK_SR;
  float onsets[TICK_MAX_ONSETS];
  float amps[TICK_MAX_ONSETS];
  int nOn = 0;

  float center = analogRead(PIN_MIC_AO) / 1023.0f;
  float env = 0, floorv = 0;
  uint32_t t0 = micros(), next = t0, lastOnsetUs = 0;
  const uint32_t windowUs = (uint32_t)(windowSeconds * 1000000.0f);
  const uint32_t refractoryUs = (uint32_t)(TICK_REFRACTORY_MS * 1000UL);

  while ((uint32_t)(micros() - t0) < windowUs && nOn < TICK_MAX_ONSETS) {
    for (;;) { int32_t rem = (int32_t)(next - micros());
               if (rem <= 0) break; if (rem > 2000) yield(); }
    next += paceUs;
    float x = analogRead(PIN_MIC_AO) / 1023.0f;
    center += 0.0006f * (x - center);
    float rect = fabsf(x - center);
    env    += 0.30f  * (rect - env);
    floorv += 0.002f * (env  - floorv);
    float excess = env - floorv; if (excess < 0) excess = 0;
    uint32_t nowUs = micros();
    if (excess > floorv * TICK_ONSET_K + TICK_ONSET_MIN &&
        (lastOnsetUs == 0 || (uint32_t)(nowUs - lastOnsetUs) > refractoryUs)) {
      onsets[nOn] = (float)(nowUs - t0) / 1000000.0f;
      amps[nOn]   = excess;
      nOn++; lastOnsetUs = nowUs;
    }
    ESP.wdtFeed();
    yield();
  }

  d.nOnsets  = (uint8_t)nOn;
  d.floorLvl = floorv;
  if (nOn > 0) { float s = 0; for (int i=0;i<nOn;i++) s += amps[i]; d.meanAmp = s/nOn; }

  // median interval + regularity (how many intervals sit near the median)
  if (nOn >= 3) {
    int m = nOn - 1;
    float iv[TICK_MAX_ONSETS];
    for (int i=0;i<m;i++) iv[i] = onsets[i+1]-onsets[i];
    for (int i=1;i<m;i++){ float v=iv[i]; int j=i-1;
      while(j>=0 && iv[j]>v){iv[j+1]=iv[j];j--;} iv[j+1]=v; }
    d.medianIntvl = iv[m/2];
    int near = 0;
    for (int i=0;i<m;i++)
      if (iv[i] > 0.75f*d.medianIntvl && iv[i] < 1.25f*d.medianIntvl) near++;
    d.regularity = (float)near / (float)m;
  }

  // Verdict. A real escapement gives regular onsets around 0.25-1.5 s.
  float expected = windowSeconds / (d.medianIntvl > 0 ? d.medianIntvl : 1.0f);
  if (nOn == 0)                                   d.verdict = 0;  // silent
  else if (nOn < 3)                               d.verdict = 1;  // too few
  else if (d.medianIntvl < 0.15f || d.regularity < 0.5f)
                                                  d.verdict = 3;  // noisy
  else if (d.medianIntvl > 2.0f)                  d.verdict = 1;  // too few
  else                                            d.verdict = 2;  // good
  (void)expected;

  Serial.printf("[TICKDIAG] onsets=%u med=%.3fs amp=%.4f floor=%.4f reg=%.2f v=%u\n",
                d.nOnsets, d.medianIntvl, d.meanAmp, d.floorLvl,
                d.regularity, d.verdict);
  return d;
}

#endif // HOST_TEST
