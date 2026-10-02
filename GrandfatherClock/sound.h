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

// Fired once per confirmed chime, AFTER its peak window closes, so `peak`
// is the true maximum rather than the threshold-crossing value.
// `onsetAgeMs` is how long ago the strike actually began — callers subtract
// it to timestamp the onset, keeping strike timing accurate.
typedef void (*ChimeCallback)(float peak, uint32_t onsetAgeMs);

void       soundBegin(ChimeCallback cb);
// Scope bins: 25ms peak-holds of `excess`, numbered from boot. Bins
// [seq - min(seq, SCOPE_RING), seq) are retained, where seq = soundScopeSeq()
// is one past the newest bin.
uint32_t   soundScopeSeq();
float      soundScopeAt(uint32_t bin);   // caller keeps `bin` in the retained range

// Coarse long-term history: one peak-excess value per second. Returns how
// many entries are valid; `outSeq` receives the total seconds ever recorded
// so callers can tell how much of the ring has been filled.
uint16_t   soundGetHistory(uint16_t* out, uint16_t maxN, uint32_t* outSeq);

// Zero-copy access to the same ring, for streaming it out without
// allocating a second full-size buffer (that duplicate cost 7.2 KB of
// precious DRAM and could overflow the data segment at link time).
uint16_t   soundHistoryCount();                 // valid entries available
uint32_t   soundHistorySeq();                   // total seconds ever recorded
uint16_t   soundHistoryAt(uint16_t i);          // i = 0 is oldest
void       soundUpdate();          // call often in loop(); self-paced
SoundState soundGetState();
void       soundNoteChimeEpoch(uint32_t epoch);  // optional external stamp

#endif // SOUND_H
