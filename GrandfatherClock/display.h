/* =====================================================================
   display.h  —  Built-in SSD1306 OLED screens (setup + live)
   ===================================================================== */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "sound.h"

void displayBegin();
void displaySplash();                                   // boot logo
void displaySetupScreen(const String& ap, const String& ip);  // AP/provisioning
void displayConnecting(const String& ssid);
void displayConnected(const String& ip);
void displayLive(const SoundState& s, float threshold, bool timeValid);
void displayChimeFlash(float peak);                     // brief chime banner

#endif // DISPLAY_H
