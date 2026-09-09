/* =====================================================================
   environment.cpp  —  Adafruit BME280 wrapper
   ---------------------------------------------------------------------
   Library: Adafruit_BME280 + Adafruit_Sensor (install both via Library
   Manager). Uses the shared Wire bus already initialized by display.cpp
   (displayBegin() calls Wire.begin() before this module runs).

   Probing: tries 0x76 then 0x77. If neither answers, the module goes
   dormant and environmentPresent() stays false — every other feature
   works fine without it (temperature compensation simply inactive).
   ===================================================================== */
#include "environment.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

static Adafruit_BME280 s_bme;
static EnvState s_state = {false, 0, 0, 0, 0};

void environmentBegin() {
  // Wire.begin() already called in displayBegin(); don't re-init the bus.
  bool ok = s_bme.begin(0x76, &Wire);
  if (!ok) ok = s_bme.begin(0x77, &Wire);
  s_state.present = ok;
  if (ok) {
    // Weather-station preset: low power, heavy filtering (slow signals).
    s_bme.setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,   // temp
                      Adafruit_BME280::SAMPLING_X1,   // pressure
                      Adafruit_BME280::SAMPLING_X1,   // humidity
                      Adafruit_BME280::FILTER_OFF);
    Serial.println(F("[ENV] BME280 found"));
    environmentUpdate();   // seed an immediate reading
  } else {
    Serial.println(F("[ENV] no BME280 (temp compensation disabled)"));
  }
}

void environmentUpdate() {
  if (!s_state.present) return;
  uint32_t now = millis();
  if (s_state.lastRead != 0 &&
      (uint32_t)(now - s_state.lastRead) < ENV_POLL_MS) return;

  s_bme.takeForcedMeasurement();          // wake, sample, sleep
  float t = s_bme.readTemperature();
  float h = s_bme.readHumidity();
  float p = s_bme.readPressure() / 100.0f; // Pa -> hPa
  if (isnan(t)) return;                     // transient bus glitch; keep last

  s_state.tempC       = t;
  s_state.humidity    = h;
  s_state.pressureHpa = p;
  s_state.lastRead    = now;
}

EnvState environmentGet()   { return s_state; }
bool     environmentPresent(){ return s_state.present; }
