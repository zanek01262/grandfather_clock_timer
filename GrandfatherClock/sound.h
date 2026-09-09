/* =====================================================================
   sound.h  —  LM393 analog chime detection (A0 envelope + peak)
   ===================================================================== */
#ifndef SOUND_H
#define SOUND_H

#include <Arduino.h>

struct SoundState {
  float    level;          // current rectified envelope (0..1)
  float    ambient;        // slow EMA baseline (0..1)
  float    peak;           // decaying peak-hold (0..1)
  uint32_t chimeCount;     // chimes since boot
  uint32_t lastChimeEpoch; // epoch secs of last chime (0 if none/no time)
  float    lastChimePeak;  // envelope peak of last chime
  // --- gain calibration telemetry (raw ADC domain) ---
  uint16_t rawMin;         // min raw ADC seen in the recent window (0..1023)
  uint16_t rawMax;         // max raw ADC seen in the recent window
  uint16_t clipCount;      // samples at/near the rails in the window
};

// Callback fired once per confirmed chime, with the peak envelope value.
typedef void (*ChimeCallback)(float peak);

void       soundBegin(ChimeCallback cb);
// Copy the most recent `n` scope bins (oldest-first) into `out`; writes the
// sequence number of the newest bin to *seq. Bins are 25ms peak-holds.
void       soundGetScope(float* out, uint8_t n, uint32_t* seq);
void       soundUpdate();          // call often in loop(); self-paced
SoundState soundGetState();
void       soundNoteChimeEpoch(uint32_t epoch);  // optional external stamp

#endif // SOUND_H
