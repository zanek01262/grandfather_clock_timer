/* =====================================================================
   ticks.h  —  Escapement tick analysis (rate + beat error)  [Phase E]
   ---------------------------------------------------------------------
   A timing-machine function: listens to the escapement's tick-tock,
   measures instantaneous rate (s/day) and beat error (ms) — the two
   numbers a clockmaker reads off a timegrapher.

   RISK: depends on the mic actually hearing the ~1Hz escapement, which
   is 30-40dB quieter than a chime. Entirely behind settings.tickEnabled
   (default OFF). If ticks are inaudible or sampling disturbs WiFi, the
   proven chime/horology core is untouched.

   Design: windowed capture (not continuous — that would fight WiFi).
   Sample A0 fast for a window, detect onsets streaming (store only
   timestamps), then analyze the interval sequence. The analysis math is
   split out (analyzeBeats) so it can be host-tested with synthetic data.
   ===================================================================== */
#ifndef TICKS_H
#define TICKS_H

#include <Arduino.h>

#define TICK_MAX_ONSETS 48    // onsets kept per capture window

struct BeatResult {
  bool     valid;
  float    beatPeriod;   // measured mean half-beat (tick->tock) seconds
  float    rateSecPerDay;// + = fast (gaining), - = slow (losing)
  float    beatErrorMs;  // tick/tock asymmetry, milliseconds
  uint8_t  nOnsets;      // onsets detected this window
  float    amplitudeRel; // mean onset strength (health proxy, 0..1)
};

// Pure analysis: given onset times (seconds, ascending) and the nominal
// beat period the movement should keep, compute rate + beat error.
// Host-testable; no hardware. nominalBeat<=0 -> auto (median interval).
BeatResult analyzeBeats(const float* onsets, int n, float nominalBeat);

// --- Tick sensitivity diagnostics (Phase E helper) ---
// A short listening window that reports whether the escapement is actually
// audible, so the user can set the LM393 pot and threshold for TICKS (which
// are 30-40 dB quieter than chimes) rather than for strikes.
struct TickDiag {
  uint8_t nOnsets;       // onsets found in the window
  float   windowS;       // window length actually used
  float   medianIntvl;   // median onset interval (s) - ~1.0 or ~0.5 if real
  float   meanAmp;       // mean onset strength above floor
  float   floorLvl;      // ambient floor during the window
  float   regularity;    // 0..1; fraction of intervals near the median
  uint8_t verdict;       // 0 silent, 1 too few, 2 good, 3 noisy/too many
};
TickDiag    ticksDiagnose(float windowSeconds);

// Hardware entry points (implemented with A0 sampling):
void        ticksArm();            // arm a capture on the next loop pass
bool        ticksArmed();
void        ticksCapture();        // blocking ~window; fills last result
BeatResult  ticksGetLast();
void        ticksSetNominal(float beatSeconds);   // learned/target beat
float       ticksGetNominal();

#endif // TICKS_H
