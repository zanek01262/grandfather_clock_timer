/* =====================================================================
   environment.h  —  BME280 environmental sensing (temp/humidity/pressure)
   ---------------------------------------------------------------------
   Shares the built-in I2C bus with the OLED (SDA=GPIO14, SCL=GPIO12).
   BME280 default address 0x76 (some boards 0x77); OLED is 0x3C, so no
   conflict. Polled slowly (~30s) — these quantities drift slowly and we
   don't want to load the bus or the loop.

   Temperature is the horologically important one: pendulum rate is
   temperature dependent, so each drift measurement is stamped with the
   temperature at which it occurred (see horology.cpp), letting the
   advisor separate real drift from seasonal swing.
   ===================================================================== */
#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <Arduino.h>

struct EnvState {
  bool  present;      // sensor found on the bus
  float tempC;        // degrees Celsius
  float humidity;     // %RH
  float pressureHpa;  // hPa
  uint32_t lastRead;  // millis() of last successful read
};

void      environmentBegin();   // probes 0x76 then 0x77
void      environmentUpdate();  // call from loop(); self-paced (~30s)
EnvState  environmentGet();
bool      environmentPresent();

#endif // ENVIRONMENT_H
