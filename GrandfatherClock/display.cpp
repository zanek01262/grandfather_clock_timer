/* =====================================================================
   display.cpp  —  SSD1306 OLED on the built-in I2C bus (GPIO12/GPIO14)
   ---------------------------------------------------------------------
   Library: Adafruit_SSD1306 + Adafruit_GFX (install both via Library Mgr).
   The board wires SDA=GPIO12 (D6), SCL=GPIO14 (D5), so we call
   Wire.begin(SDA, SCL) explicitly before initializing the panel.

   Screens:
     splash       -> boot identity
     setup        -> AP name + IP for provisioning
     connecting   -> spinner-ish "joining <ssid>"
     connected    -> shows station IP briefly
     live         -> ambient floor, live level bar, threshold marker,
                     chime count + last-chime age
     chimeFlash   -> momentary inverted banner when a chime fires
   ===================================================================== */
#include "display.h"
#include "config.h"

#include <Wire.h>
#include <time.h>              // time(nullptr) used in displayLive()
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
static bool g_ok = false;

// Latch a chime flash for a short, non-blocking window.
static uint32_t g_flashUntil = 0;
static float    g_flashPeak  = 0;

// Ancillary live-screen state (Phase A/B), pushed from the main loop.
static bool  g_stopped   = false;
static bool  g_windDue   = false;
static bool  g_envOk     = false;
static float g_tempC     = 0;
void displaySetAlerts(bool stopped, bool windDue, bool envPresent, float tempC) {
  g_stopped = stopped; g_windDue = windDue; g_envOk = envPresent; g_tempC = tempC;
}

void displayBegin() {
  // Some integrated boards gate the OLED behind a reset line on GPIO16;
  // pulse it first (harmless if not wired). D0 is otherwise unused.
  pinMode(16, OUTPUT);
  digitalWrite(16, LOW);  delay(20);
  digitalWrite(16, HIGH); delay(20);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  // 400kHz: a full 1 KB frame is still >=23 ms of I2C (~92 ms at 100kHz),
  // during which the mic isn't sampled. The dashboard's "longest gap"
  // readout shows the real figure.
  Wire.setClock(400000);
  g_ok = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!g_ok) { Serial.println(F("[OLED] init failed")); return; }
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.display();
}

void displaySplash() {
  if (!g_ok) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 4);   oled.println(F("GRANDFATHER CLOCK"));
  oled.setCursor(0, 16);  oled.println(F("chime monitor"));
  oled.drawLine(0, 28, OLED_W - 1, 28, SSD1306_WHITE);
  oled.setCursor(0, 36);  oled.println(F("v" FW_VERSION));
  oled.setCursor(0, 50);  oled.println(F("booting..."));
  oled.display();
}

void displaySetupScreen(const String& ap, const String& ip) {
  if (!g_ok) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);  oled.println(F("SETUP MODE"));
  oled.drawLine(0, 10, OLED_W - 1, 10, SSD1306_WHITE);
  oled.setCursor(0, 16); oled.println(F("1. Join WiFi:"));
  oled.setCursor(6, 26); oled.println(ap);
  oled.setCursor(0, 38); oled.println(F("2. Open browser:"));
  oled.setCursor(6, 48); oled.println(ip);
  oled.display();
}

void displayConnecting(const String& ssid) {
  if (!g_ok) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 8);  oled.println(F("Connecting to"));
  oled.setTextSize(1);
  oled.setCursor(0, 24); oled.println(ssid);
  oled.setCursor(0, 48); oled.println(F("please wait..."));
  oled.display();
}

void displayConnected(const String& ip) {
  if (!g_ok) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 8);  oled.println(F("CONNECTED"));
  oled.drawLine(0, 18, OLED_W - 1, 18, SSD1306_WHITE);
  oled.setCursor(0, 26); oled.println(F("Dashboard at"));
  oled.setCursor(0, 40); oled.println(ip);
  oled.display();
  delay(1500);   // let the user read the IP before live view takes over
}

// Map a 0..1 value to a bar width, clamped.
static int barW(float v) {
  if (v < 0) v = 0; if (v > 1) v = 1;
  return (int)(v * (OLED_W - 1));
}

void displayLive(const SoundState& s, float threshold, bool timeValid) {
  if (!g_ok) return;

  // If a chime flash is active, draw the banner instead.
  if (millis() < g_flashUntil) {
    oled.clearDisplay();
    oled.fillRect(0, 0, OLED_W, OLED_H, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setTextSize(2);
    oled.setCursor(14, 12); oled.println(F("CHIME"));
    oled.setTextSize(1);
    oled.setCursor(18, 40); oled.print(F("peak "));
    oled.print(g_flashPeak, 3);
    oled.setTextColor(SSD1306_WHITE);
    oled.display();
    return;
  }

  // Stopped-clock takes over the screen — it's the most important alert.
  if (g_stopped) {
    oled.clearDisplay();
    oled.setTextSize(2);
    oled.setCursor(10, 10); oled.println(F("CLOCK"));
    oled.setCursor(6, 30);  oled.println(F("STOPPED"));
    oled.setTextSize(1);
    oled.setCursor(0, 52);  oled.print(F("no strike heard"));
    oled.display();
    return;
  }

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  // Header: chime count + time-valid indicator.
  oled.setCursor(0, 0);
  oled.print(F("chimes:"));
  oled.print(s.chimeCount);
  oled.setCursor(96, 0);
  oled.print(timeValid ? F("NTP") : F("---"));

  // Excess level (level - ambient) drives the meter; same as detector.
  float excess = s.level - s.ambient;
  if (excess < 0) excess = 0;

  // Level bar.
  oled.setCursor(0, 14); oled.print(F("lvl"));
  oled.drawRect(22, 13, OLED_W - 22, 9, SSD1306_WHITE);
  int lw = barW(excess) * (OLED_W - 24) / (OLED_W - 1);
  oled.fillRect(23, 14, lw, 7, SSD1306_WHITE);

  // Peak-hold tick.
  int px = 23 + barW(s.peak) * (OLED_W - 24) / (OLED_W - 1);
  if (px > OLED_W - 2) px = OLED_W - 2;
  oled.drawLine(px, 12, px, 22, SSD1306_WHITE);

  // Threshold marker on its own row.
  oled.setCursor(0, 26); oled.print(F("thr"));
  oled.drawRect(22, 25, OLED_W - 22, 5, SSD1306_WHITE);
  int tx = 23 + barW(threshold) * (OLED_W - 24) / (OLED_W - 1);
  if (tx > OLED_W - 2) tx = OLED_W - 2;
  oled.drawLine(tx, 24, tx, 31, SSD1306_WHITE);

  // Ambient floor readout.
  oled.setCursor(0, 36);
  oled.print(F("ambient "));
  oled.print(s.ambient, 3);
  if (g_envOk) {
    oled.setCursor(92, 36);
    oled.print(g_tempC, 1); oled.print((char)247); oled.print('C');  // 247 = degree
  }
  if (g_windDue) { oled.setCursor(92, 0); oled.print(F("WIND")); }

  // Last chime age.
  oled.setCursor(0, 48);
  oled.print(F("last: "));
  if (s.lastChimeEpoch == 0) {
    oled.print(F("--"));
  } else if (timeValid) {
    long age = (long)time(nullptr) - (long)s.lastChimeEpoch;
    if (age < 0) age = 0;
    if (age < 90)        { oled.print(age);      oled.print(F("s ago")); }
    else if (age < 5400) { oled.print(age / 60); oled.print(F("m ago")); }
    else                 { oled.print(age / 3600); oled.print(F("h ago")); }
  } else {
    oled.print(F("(no time)"));
  }

  oled.display();
}

void displayChimeFlash(float peak) {
  g_flashPeak  = peak;
  g_flashUntil = millis() + 700;   // banner shows on next displayLive() tick
}
