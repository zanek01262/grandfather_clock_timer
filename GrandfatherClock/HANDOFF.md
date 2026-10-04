# Grandfather Clock Chime Monitor — Handoff (v3.0.0)

**Purpose of this doc:** everything a new person (or a new AI session) needs to
pick this project up cold. Read this first; `project_summary_v3.0.0.md` is the
compact architecture/API reference alongside it.

> **v3.0.0 is chime detection only.** Everything else from v2.x — horology
> (drift rate, adjustment advisor, adjustment/wind logs, stopped-clock alarm,
> half-hour mode), the timegrapher/tick analysis, and the BME280 temperature
> sensor — was removed. Any doc or code comment describing those is from the
> v2 era. The full v2 history is in git (last v2 release: 2.16.2).

---

## 1. What this device is

An ESP8266 that listens to a grandfather clock through an analog microphone,
detects each chime/strike, and logs it with a millisecond, NTP-referenced
timestamp and its loudness (peak). You watch and tune it from a web dashboard
(live signal plot, threshold/refractory sliders, mic gain meter, chime log
viewer, Excel/CSV export); the built-in OLED shows the level, chime count and
a CHIME banner.

---

## 2. Hardware (3 wires)

| Part | Connection | Notes |
|---|---|---|
| **FORIOT ESP8266 NodeMCU** w/ built-in SSD1306 OLED | — | Board select: **NodeMCU 1.0 (ESP-12E)** |
| **OLED** (built in) | **SDA=GPIO14 (D5), SCL=GPIO12 (D6)**, addr 0x3C | ⚠ Vendor listing has these **REVERSED**. Verified by I²C scan. |
| **LM393 analog mic** | AO→A0, VCC→**3V3** (not 5V), GND→GND | 3V3 keeps A0 in range; the 10k/22k divider fallback was never needed |

`displayBegin()` also pulses GPIO16 low→high (Heltec-style OLED reset; harmless
if unwired). Storage is LittleFS on onboard flash. D0–D4, D7, D8 are free. See
`wiring_diagram.svg`.

**If the OLED is ever blank:** flash `OLED_Finder.ino`, open Serial at 115200,
and it reports the true pin pair + address in ~2 seconds. Never trust a vendor
pinout on this project.

---

## 3. Build & flash

- **Board:** NodeMCU 1.0 (ESP-12E Module), ESP8266 community core (3.1.2 tested)
- **Flash Size:** must include FS space — e.g. **"4MB (FS:2MB)"**.
  With `FS:none`, settings and the chime log silently fail (serial warns).
- **Libraries:** Adafruit_SSD1306, Adafruit_GFX (+ Adafruit_BusIO). mDNS/OTA
  ship with the core. The BME280/Adafruit_Sensor libraries are no longer needed.
- **Upload:** USB, **browser at `http://<device>/update`** (Sketch → Export
  Compiled Binary, upload the .bin — bypasses the IDE's flaky network ports),
  or IDE OTA (network port `grandfatherclock`).
- **After flashing any UI change: hard-refresh the browser (Ctrl+Shift+R).**
  The page is embedded in the firmware; a cached page looks like a frozen device.

---

## 4. Setup flow

1. No saved credentials → device raises SoftAP **`GrandfatherClock-Setup`**
   (open network) and serves a setup page at **http://192.168.4.1**.
2. Pick the network, enter the password, submit → saved to `/config.json`,
   reboot, join WiFi.
3. Dashboard at **http://grandfatherclock.local** (or the IP the OLED shows
   briefly on the "CONNECTED" screen).
4. "Reset WiFi" on the dashboard returns it to setup mode.

If the saved network is unreachable at boot (e.g. after a power cut — the
router takes minutes to come back), the device falls back to setup mode and
**retries the saved network every 60 s** while no phone is connected to the
setup AP, rebooting into normal mode once it joins.

**iPhone gotcha:** on the setup AP, turn **Cellular Data off**, or iOS routes
192.168.4.1 over LTE.

---

## 5. Firmware layout

```
GrandfatherClock.ino   orchestration, HTTP routes, NTP/time zone, WiFi modes, loop
config.h               pins, tunables, paths, FW_VERSION + version history
settings.h/.cpp        persisted settings, hand-rolled JSON (no ArduinoJson)
storage.h/.cpp         LittleFS mount, chime log append/rotate, local-time formatting
sound.h/.cpp           A0 sampling, envelope detection, scope bins, 1 h history, telemetry
analysis.h/.cpp        optional tone filter: FFT chime learning + Goertzel verify
display.h/.cpp         OLED screens
webpages.h             setup page + dashboard as PROGMEM raw strings
```

**Design rules that matter:**
- Hot API paths (`/api/state` runs ~5×/s for months) use `snprintf` into
  fixed buffers, never `String` concat (heap fragmentation).
- Canvas colours come from CSS variables (`--grid`/`--trace`/`--thresh`/
  `--axis`) — retheming is a `:root` edit only.

---

## 6. How detection works

`soundUpdate()` runs every `loop()` and reads A0 at most every 2 ms (~500 Hz;
the gain panel's **samples/s** and **longest gap** show what is really achieved).

```
A0 raw -> /1023 -> |x - center|        center: very slow EMA (DC bias)
       -> envelope = EMA(0.25)         fast follower of the rectified signal
       -> ambient  = asymmetric EMA    rises ~25 s, falls ~0.5 s (quiet floor)
       -> excess   = envelope - ambient (clamped >= 0)
       -> excess >= threshold and refractory elapsed -> chime candidate
       -> [tone filter on: 32 ms 8 kHz burst must match the learned pitch]
       -> peak = max excess over the next 400 ms -> chime logged
```

- Defaults: threshold **0.18**, refractory **1200 ms** (tune on the dashboard).
- The chime is logged with its **onset** time (the 400 ms peak window is wound
  back) and only once NTP time is valid. The Log viewer explains an empty log.
- AO is the raw waveform sampled far below chime pitch, so each reading lands
  at a random point of the cycle; identical strikes read roughly ±10 %.
  A peak-hold envelope was tried in 2.16.0 to tighten that and was **reverted**
  in 2.16.2 — on the real device chimes stopped registering. Don't reintroduce
  it without on-device evidence.
- **Live plot:** 25 ms peak bins (fixed cadence), 256-bin ring on the device;
  the page asks `/api/state?since=<bin>` for exactly what it lacks, so slow
  responses are caught up instead of dropped. Gaps older than the ring are
  backfilled from the 1-hour history (drawn fainter, 1 s resolution).
  Auto-scale always keeps the threshold line in view.
- Log-streaming handlers keep sampling (`soundSampleOnly()`); chimes detected
  meanwhile are queued and logged afterwards with their true onset time.

---

## 7. HTTP API

**STA mode:** `/` · `/api/state?since=<bin>` · `/api/config` GET/POST
(`threshold`, `refractoryMs`, `toneEnabled`, `toneRatio`, `tz` — POSIX TZ rule,
legacy `tzOffset` still accepted) · `/api/log` (CSV) · `/api/logstat` ·
`/api/learn/start` (POST) · `/api/learn/status` · `/api/gain` ·
`/api/history?n=<entries>` · `/api/reset` · `/update`

**AP mode:** `/` · `/scan` · `/rescan` · `/save`

---

## 8. Storage (LittleFS)

| File | Contents | Rotation |
|---|---|---|
| `/config.json` | WiFi creds, threshold, refractory, tone filter, time zone | overwritten |
| `/chimes.csv` | epoch_ms, peak per chime | 128 KB → `/chimes.old.csv` |

`/api/log` streams old+current as `date,time,epoch_ms,sec_from_hour,peak`
(local time per the configured time zone; `sec_from_hour` is signed seconds
from the nearest top of the hour).

Files written by v2 firmware (`/drift.csv`, `/adjust.csv`, `/wind.csv`,
`/tick.csv`, `/tick.csv.old`) are left on flash untouched; v3 never reads them.

---

## 9. Testing

- **Compile check:** arduino-cli with FQBN `esp8266:esp8266:nodemcuv2:eesz=4M2M`.
- **Page JS:** extract the `<script>` blocks from `webpages.h` and run
  `node --check`.
- **Host test suite** (`GrandfatherClock_hosttests.zip`, kept outside the repo):
  its horology/ticks checks target code removed in v3 and need pruning before
  it will build again.
- **Only verifiable on real hardware:** real acoustics, ADC noise under WiFi,
  loop timing. Simulations of the detector have been wrong about hardware
  before (see the 2.16.0 envelope above).

> **32-bit vs 64-bit:** the host's `unsigned long` is 64-bit, the ESP8266's is
> 32-bit. Arithmetic on `millis()`/`micros()` must be reasoned about in
> `uint32_t` terms (wrap-safe subtraction); a host test can't catch it.

---

## 10. Hard-won lessons (do not relearn these)

1. **Never trust vendor pinouts.** OLED SDA/SCL were listed reversed.
2. **ESP8266 ADC fights WiFi.** No fast `analogRead` in AP mode; sampling only
   runs in STA mode, at ≤500 Hz.
3. **Blocking WiFi scans drop connected AP clients** — scan before raising the
   AP; rescan asynchronously.
4. **Never `delay()`/`ESP.restart()` inside an HTTP handler.** Defer via
   `g_rebootAt` checked in `loop()`.
5. **No external resources on the AP setup page** — captive clients have no
   internet.
6. **Anything that blocks `loop()` deafens the detector** (OLED pushes, long
   handlers). The OLED refreshes at 2 Hz for this reason.
7. **String concat in hot API paths fragments the heap** — use snprintf.
8. **A cached browser page looks exactly like a frozen device.** Hard-refresh.
9. **iPhone + no-internet AP:** cellular data hijacks 192.168.4.1.
10. **`configTime(offset, 0, …)` has no daylight saving** — use a POSIX TZ rule.
11. **Validate detector changes on the device, not just in simulation.**

---

## 11. Known open items

- A strike that rings above the threshold for longer than the refractory can
  re-trigger and log an extra row. Mitigation: set Refractory just under the
  clock's strike spacing.
- `/update` has no password (set `OTA_PASSWORD` in `config.h`), and
  `/api/reset` wipes WiFi settings on a plain GET.
- `saveSettings()` truncates then rewrites `config.json`; a power cut mid-save
  can lose settings (device falls back to setup mode).
- The `settings.cpp` header comment contains an example SSID/password — replace
  it if those are real.

---

## 12. Working agreement

- **Semver:** MAJOR = breaking hardware/storage/feature removal, MINOR =
  features, PATCH = fixes. `FW_VERSION` lives in `config.h` and is reported on
  the serial banner, OLED splash and `/api/state`'s `fw` field (that's how you
  confirm an update took).
- **Keep exactly one project summary.** Delete superseded ones.
