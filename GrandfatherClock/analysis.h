/* =====================================================================
   analysis.h  —  Spectral tools: chime learning (FFT) + tone gate
   ---------------------------------------------------------------------
   Learning: arm via analysisLearnArm(); the next envelope trigger
   captures a ~8kHz burst, FFTs it, and stores the dominant peaks.
   Detection: analysisVerifyTone() captures a short burst and checks
   that energy at the learned frequencies dominates (Goertzel).
   ===================================================================== */
#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <Arduino.h>

struct LearnResult {
  bool  valid;     // a capture has completed since boot
  float f1, f2;    // dominant / secondary peak Hz (f2==0 if none)
  float mag2rel;   // f2 magnitude relative to f1 (0..1)
  float sr;        // measured sample rate of the capture
};

void        analysisLearnArm();       // arm; auto-disarms after 60s
bool        analysisLearnArmed();
LearnResult analysisGetLearn();
void        analysisCaptureLearn();   // called by sound.cpp on trigger

// Tone gate for detection: true = candidate matches the learned chime.
// If nothing has been learned, always returns true (envelope-only mode).
bool        analysisVerifyTone();

#endif // ANALYSIS_H
