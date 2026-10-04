# Grandfather Clock Chime Monitor — Project Summary (v3.0.0)

Compact architecture and API reference. Start with `HANDOFF.md`.

## Scope

Chime detection only: listen on an analog mic, detect each chime/strike, log
it with an NTP-referenced millisecond timestamp and its peak loudness, and show
it on a web dashboard and the built-in OLED. v3.0.0 removed the v2 horology
(drift/advisor/adjustments/wind/stopped alarm/half-hour), tick analysis and the
BME280 sensor.

## Modules

- **sound**: ≤500 Hz A0 sampling from `loop()` (STA mode only); DC-tracking
  center, rectify, envelope EMA 0.25, asymmetric ambient (up 0.00008, down
  0.004 per sample), excess = envelope − ambient; threshold + refractory
  trigger; optional tone verify; 400 ms peak window; completed chimes queued
  and delivered by `soundUpdate()` (`soundSampleOnly()` samples without
  delivering, for long HTTP handlers). Also: 25 ms scope bins at a fixed
  cadence in a 256-bin ring; a 3600-entry history where each entry is exactly
  40 bins (1 s); gain telemetry (raw min/max/clip) and sampling telemetry
  (samples/s, longest gap).
- **analysis**: tone filter. Learn = 1024-sample ~8 kHz burst → Hann → FFT →
  top peak(s) stored as `toneF1/F2`. Verify = 256-sample burst → Goertzel at
  f−df/f/f+df; pass if the tone energy fraction ≥ `toneRatio`.
- **storage**: LittleFS; `/chimes.csv` (`epoch_ms,peak`) rotating at 128 KB to
  `/chimes.old.csv`; local date/time formatting via the POSIX TZ rule.
- **settings**: `/config.json` — ssid, pass, threshold, refractoryMs,
  tzOffset (legacy), tz, toneEnabled, toneF1, toneF2, toneRatio.
- **display**: SSD1306 screens — splash, setup, connecting, connected (IP),
  live (level bar, threshold marker, ambient, chime count, last-chime age),
  CHIME banner. Refreshed every 500 ms (each push blocks sampling ~23 ms).
- **network**: setup SoftAP `GrandfatherClock-Setup` (192.168.4.1) with
  retry of the saved network every 60 s while no client is connected; STA mode
  with mDNS `grandfatherclock.local`, browser OTA at `/update`, ArduinoOTA.
- **dashboard** (`webpages.h`): live plot (linear/log, auto/fixed Y, 10 s–5 min
  live windows, 15 min–1 h from device history, threshold always in view),
  Detection tuning (threshold, refractory, Learn chime, tone filter),
  Microphone gain, chime Log viewer, Excel/CSV export, time zone, firmware
  update, Reset WiFi.

## HTTP API

| Route | Method | Purpose |
|---|---|---|
| `/` | GET | dashboard (STA) / setup page (AP) |
| `/api/state?since=<bin>` | GET | level, ambient, peak, threshold, chimes, lastChime, lastPeak, timeValid, epoch, fw, scopeFrom, scopeSeq, scope[] (≤64 bins) |
| `/api/config` | GET | threshold, refractoryMs, tzOffset, tz, localTime |
| `/api/config` | POST | threshold, refractoryMs, toneEnabled, toneRatio, tz (or legacy tzOffset) |
| `/api/log` | GET | CSV `date,time,epoch_ms,sec_from_hour,peak` |
| `/api/logstat` | GET | fs, timeValid, chimeBytes, chimesSinceBoot, why (empty-log reason) |
| `/api/learn/start` | POST | arm tone learning (60 s) |
| `/api/learn/status` | GET | armed, valid, f1, f2, sr, enabled, ratio |
| `/api/gain` | GET | rawMin, rawMax, swing, clip, verdict, advice, sps, gapMs |
| `/api/history?n=<entries>` | GET | header `scale,seq,count,firstBin,binsPerEntry` + comma-separated values |
| `/api/reset` | GET | clear WiFi creds and reboot to setup mode |
| `/update` | GET/POST | browser firmware upload |
| `/scan`, `/rescan`, `/save` | — | AP-mode setup |

## Versioning

`FW_VERSION` and the full version history live in `config.h`. Recent:
2.15.1 scope dropout fix · 2.15.2 history backfill · 2.16.0 sampling
telemetry/OLED 2 Hz · 2.16.1 time zone with DST, AP retry, sampling during
streaming · 2.16.2 envelope revert, plot keeps threshold in view ·
**3.0.0 chime detection only**.
