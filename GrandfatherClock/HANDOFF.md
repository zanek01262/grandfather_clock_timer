# Grandfather Clock Tool — Handoff (v2.10.0)

**Purpose of this doc:** everything a new person (or a new AI session) needs to
pick this project up cold. Read this first; `project_summary_v2.10.0.md` is the
detailed architecture reference alongside it.

> **IMPORTANT — stale docs.** Any document describing an **SD card**, **SdFat**,
> or **OLED SDA=GPIO12/SCL=GPIO14** is obsolete and *wrong*. The SD card was
> removed at v2.0.0 and the OLED pins are **reversed** from the vendor listing.
> Delete older summaries rather than keeping them "for reference" — they have
> caused repeated confusion.

---

## 1. What this device is

An ESP8266-based **acoustic chime monitor and horological instrument** for a
grandfather clock. It listens to the clock, identifies genuine chimes by their
learned frequency signature, measures how far each hour-strike lands from the
true top of the hour (NTP-referenced), computes the pendulum's drift rate, and
recommends rating-nut adjustments using a sensitivity model it learns from the
user's own past adjustments. It also measures escapement tick rate and beat
error (timegrapher), logs environment, and warns if the clock stops.

Interfaces: a responsive web dashboard (Brass & Walnut theme), a 0.96" OLED on
the device, and a JSON HTTP API.

---

## 2. Hardware (verified — 3 wires for the mic, +4 for the optional BME280)

| Part | Connection | Notes |
|---|---|---|
| **FORIOT ESP8266 NodeMCU** w/ built-in SSD1306 OLED | — | Board select: **NodeMCU 1.0 (ESP-12E)** |
| **OLED** (built in) | **SDA=GPIO14 (D5), SCL=GPIO12 (D6)**, addr 0x3C | ⚠ Vendor listing has these **REVERSED**. Verified by I²C scan. |
| **LM393 analog mic** | AO→A0, VCC→**3V3** (not 5V), GND→GND | 3V3 keeps A0 in range; the 10k/22k divider fallback was never needed |
| **BME280** (optional) | SDA/SCL → same I²C bus, addr 0x76 or 0x77 | Auto-probes both; absent = temp compensation simply inactive |

`displayBegin()` also pulses GPIO16 low→high (Heltec-style OLED reset; harmless
if unwired). **No SD card.** Storage is LittleFS on onboard flash. D0–D4, D7,
D8 are free.

**If the OLED is ever blank:** flash `OLED_Finder.ino` (kept in outputs), open
Serial at 115200, and it reports the true pin pair + address in ~2 seconds.
Never trust a vendor pinout on this project.

---

## 3. Build & flash

- **Board:** NodeMCU 1.0 (ESP-12E Module), ESP8266 community core
- **Flash Size:** must include FS space — e.g. **"4MB (FS:2MB)"**.
  With `FS:none`, settings and all logs silently fail (serial warns).
- **Libraries:** Adafruit_SSD1306, Adafruit_GFX, Adafruit_BME280,
  Adafruit_Sensor. (mDNS/OTA ship with the core; SdFat is **not** used.)
- **Upload:** USB (COM port) or **OTA** — Tools → Port → network port
  `grandfatherclock`. OTA discovery in IDE 2.x is flaky; if the network port
  doesn't appear, restart the IDE with the device already running, allow the
  IDE through Windows Firewall, and confirm both are on the same subnet.
  USB always works and is the pragmatic fallback.
- **After flashing any UI change: hard-refresh the browser (Ctrl+Shift+R).**
  A cached page looks exactly like a frozen device. This has bitten us.

---

## 4. First-run / setup flow

1. No saved credentials → device raises SoftAP **`GrandfatherClock-Setup`**
   (open network) and serves a splash at **http://192.168.4.1**.
2. User picks their network, enters the password, submits.
3. Device saves to `/config.json`, reboots, joins WiFi.
4. Dashboard thereafter at **http://grandfatherclock.local** (or its IP; the
   OLED shows the IP briefly on the "CONNECTED" screen).
5. "Reset WiFi" on the dashboard returns it to setup mode.

**iPhone gotcha:** on the setup AP, turn **Cellular Data off**. iOS sees the
no-internet AP and routes requests over LTE, so 192.168.4.1 never resolves.

---

## 5. Firmware layout (19 files)

```
GrandfatherClock.ino   orchestration, HTTP routes, loop scheduling
config.h               ALL pins, tunables, paths, FW_VERSION
settings.h/.cpp        persisted settings, hand-rolled JSON (no ArduinoJson)
storage.h/.cpp         LittleFS mount + chime log append/rotate
sound.h/.cpp           500 Hz A0 sampling, envelope detection, scope bins, gain telemetry
analysis.h/.cpp        FFT chime learning + Goertzel tone gate
horology.h/.cpp        strike events, offsets, drift regression, advisor, stopped/wind
ticks.h/.cpp           escapement capture + beat analysis (rate, beat error)
environment.h/.cpp     BME280 wrapper
display.h/.cpp         OLED screens
webpages.h             both web pages as PROGMEM raw strings
```

**Design rules that matter:**
- `horology.cpp` and `ticks.cpp` are deliberately **hardware-agnostic** —
  temperature, half-hour mode, and wind interval are *pushed in* via setters
  from the loop. This is what makes them host-testable. Keep it that way.
- Hot API paths use `snprintf` into fixed buffers, never `String` concat
  (heap fragmentation on a device meant to run for months).
- Canvas colors come from CSS variables (`--grid`/`--trace`/`--thresh`/
  `--axis`) — **retheming is a `:root` edit only**, no drawing-code changes.

---

## 6. How the horology actually works

**Strike → measurement.** Detected strikes group into an event (6 s of silence
closes it). The count is compared to the expected local hour (13:00→1,
00:00→12). Offset = first strike vs. nearest top-of-hour, ms precision,
**+ = late**. Valid requires count match AND |offset| < 600 s, which
auto-rejects miscounts and (when half-hour mode is off) :30 single strikes.

**Drift rate.** Least-squares regression of offset vs. time over valid points
**since the last logged adjustment**. Needs ≥2 points spanning ≥2 h. Units:
s/day.

**The advisor.** Each logged adjustment stores `(epoch, turns, rateBefore)`.
Comparing rate before vs. after an adjustment yields **k** = s/day per turn.
Recommendation = `−rate / k`. **It cannot predict until one full adjustment
cycle has completed** — that's inherent; no dataset exists for this clock's
screw pitch until the user creates one.

**Sign convention (user-consistent, keep it):** `+ turns = raising the bob =
speeding up`.

**Note:** correcting the *rate* does not correct accumulated *offset*. Once
drift is near zero, the user hand-sets the minute hand once and it stays.

---

## 7. HTTP API

**STA mode:** `/` · `/api/state` · `/api/config` (GET/POST) · `/api/log` ·
`/api/drift` · `/api/horology` · `/api/adjust` (POST turns) · `/api/wind`
(POST) · `/api/env` · `/api/gain` · `/api/learn/start` (POST) ·
`/api/learn/status` · `/api/tick` · `/api/tick/arm` (POST) ·
`/api/tick/calibrate` (POST) · `/api/tick/log` · `/api/reset`

**AP mode:** `/` · `/scan` · `/rescan` · `/save`

`/api/config` POST accepts: `threshold`, `refractoryMs`, `tzOffset`,
`toneEnabled`, `toneRatio`, `halfHour`, `windDays`, `tickEnabled`.

---

## 8. Storage (LittleFS, ~2 MB)

| File | Contents | Rotation |
|---|---|---|
| `/config.json` | all settings incl. WiFi creds | overwritten |
| `/chimes.csv` | epoch, peak per strike | 128 KB → `/chimes.old.csv` |
| `/drift.csv` | epoch,count,expected,offset,valid,tempC,isHalf | **none** (deliberate) |
| `/adjust.csv` | epoch,turns,rateBefore | none (tiny) |
| `/wind.csv` | epoch per winding | none (tiny) |
| `/tick.csv` | epoch,beat,rate,beatErrorMs,amplitude | 128 KB → `.old` |

`drift.csv` is deliberately **not** rotated: it's ~1–2 KB/day (years of
headroom) and it is exactly the long-baseline history that makes the drift
model valuable. Revisit in a few years, not sooner.

---

## 9. Testing

**Host suite** (`GrandfatherClock_hosttests.zip`): compiles the *real*
`analysis/horology/ticks/settings` sources against mock Arduino/LittleFS
headers with g++. **100 checks, all passing at v2.10.0.** Covers FFT peak
recovery, Goertzel accept/reject matrix, event/offset/hour-mapping (incl.
negative offsets, midnight, half-hour), regression + k-learning + prediction,
beat rate/beat-error math, ring-buffer wrap, stopped-clock durations, settings
JSON round-trip, CSV persistence across simulated reboot. Embedded page JS is
syntax-checked with `node --check`; pages are rendered headless and screenshot
for visual review.

Run: extract next to the sketch folder, `./run_tests.sh` (needs g++/WSL).
Claude runs this in-sandbox before every release.

> ### ⚠ Known blind spot — read before trusting a green suite
> The host's `unsigned long` is **64-bit**; the ESP8266's is **32-bit**.
> The suite therefore **cannot** catch integer-width overflow bugs. This
> already caused a real one: the stopped-clock alarm silently cleared itself
> after ~49.7 days because `secs * 1000` wrapped uint32 (fixed in v2.8.1 by
> comparing in seconds). **Any arithmetic on `millis()`, micros, or ms
> conversions must be reasoned about separately**, and where practical tested
> in explicit `uint32_t` form.

**Only verifiable on real hardware:** the Arduino IDE compile, ADC burst
timing under WiFi, and real acoustics.

---

## 10. Hard-won lessons (do not relearn these)

1. **Never trust vendor pinouts.** OLED SDA/SCL were listed reversed.
2. **ESP8266 ADC fights WiFi.** No fast `analogRead` in AP mode; bursts only,
   brief, with *measured* (not assumed) sample rate.
3. **Blocking WiFi scans drop connected AP clients** — i.e. the phone loading
   the setup page. Scan *before* raising the AP; rescan asynchronously.
4. **Never `delay()`/`ESP.restart()` inside an HTTP handler.** Defer via
   `g_rebootAt` checked in `loop()`, or the response never flushes.
5. **No external resources on the AP splash.** Captive clients have no
   internet; a render-blocking font fetch hangs the page. (Verified: 0
   external refs.)
6. **A blocking capture deafens the detector.** `ticksCapture()` blocks ~12 s,
   during which `soundUpdate()` doesn't run and strikes are missed. Captures
   defer while a strike event is open and within 90 s of :00 (and :30 in
   half-hour mode). **Hour accuracy outranks tick sampling.**
7. **String concat in hot API paths fragments the heap** — use snprintf.
8. **A cached browser page looks exactly like a frozen device.** Hard-refresh
   before debugging firmware.
9. **iPhone + no-internet AP:** cellular data hijacks 192.168.4.1.
10. **32-bit vs 64-bit** — see the blind-spot box above.

---

## 11. Current state & open items

**Working and verified on hardware:** provisioning, mDNS, OTA, dashboard,
scope streaming, OLED, tap-level chime detection, gain calibration tool.

**Built and unit-tested, but never exercised against a real clock:**
- Learn mode + tone filter (needs a real chime to learn from)
- Hourly offset capture and drift regression (needs hours of real strikes)
- The advisor (needs one complete adjustment cycle to learn *k*)
- **Escapement tick analysis** — the highest-risk feature. Two assumptions are
  unproven: (a) that the LM393 can hear the escapement at real mic distance
  (ticks are 30–40 dB below chimes), and (b) that 12 s captures don't disturb
  WiFi. It ships **default-OFF** and fully isolated for exactly this reason.
  First real test: enable it, watch serial for `[TICK] onsets=N` — N ≈ 12–24
  per 12 s window means the escapement is being heard.

**Suggested next steps:**
- Periodic STA retry from AP fallback (self-heal after a router outage; today
  a boot during an outage parks the device in setup mode until rebooted)
- Drift/offset history chart on the dashboard, with adjustment markers
- Phase F leftovers: clock profiles + exportable service report
- Health trending (strike amplitude / decay over months → "service due")

---

## 12. Working agreement (how this project has been run)

- **Semver:** MAJOR = breaking hardware/storage, MINOR = features,
  PATCH = fixes. `FW_VERSION` lives in `config.h` and is reported on the
  serial banner, OLED splash, and `/api/state`'s `fw` field — that last one is
  how you confirm an OTA actually took.
- **Tests are written alongside features, not after.** The beat-analysis math
  was developed test-first and the suite caught two real bugs before hardware.
- **Every release ships:** firmware zip (all sources + summary), the host test
  suite, and an updated `project_summary_vX.Y.Z.md`.
- **Keep exactly one summary.** Delete superseded ones immediately.
