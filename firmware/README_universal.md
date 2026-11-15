# Plant sensor – universal firmware

One firmware for every probe. No per-probe files, no WiFi or MQTT settings in the code.

| File | What |
| --- | --- |
| `plant_sensor_universal.yaml` | **Flash this** – bare probes, English names |
| `plant_sensor_universal_pl.yaml` | **Flash this** – bare probes, Polish names |
| `plant_sensor_universal_env.yaml` / `_env_pl.yaml` | Same + SHT40 / LTR-303 / SCD41 sensors |
| `plant_sensor_base.yaml`, `sensors_env.yaml` | Shared parts (included by the files above) |
| `plant_log.h`, `partitions_plant.csv` | Measurement log in its own flash area |
| `docs/` | Phone app (installable page, works offline): Bluetooth history, chart, calibration, settings, backup, PL/EN |
| `secrets.yaml` | Only `ota_password`, `web_username`, `web_password` |

Keep all files in one folder. The language only changes the names on the probe's web page and in
Home Assistant; the probe works the same. (The web page frame and the WiFi setup page come from
ESPHome and stay in English.)

## 1. Build and flash (once per probe, over USB)
After a USB upload in download mode the ESP32-C3 does not restart by itself: press **RST**.
The flash layout changes, so the first upload must be over USB:
1. Hold BOOT, press RST, release BOOT (download mode).
2. `py -m esphome run plant_sensor_universal_pl.yaml` (or another of the four) → choose the COM port.
The same build goes on every probe. The first build downloads ESP-IDF and takes a while.

## Board (PCB version and module)
Every top-level file has one line that selects the pin map:
`board: !include board_v2.yaml` – change it before compiling:

| File | Use for |
| --- | --- |
| `board_v2.yaml` | PCB v2 (the boards you have now), ESP32-C3 SuperMini |
| `board_v3_supermini.yaml` | PCB v3 with a SuperMini (JP1/JP3/JP4 on pads "S", JP2 fitted) |
| `board_v3_zero.yaml` | PCB v3 with a Waveshare ESP32-C3-Zero (JP1/JP3/JP4 on pads "Z", JP2 empty) |

The web page shows the firmware version with the board name, e.g. "0.9.3 alpha (v2 SuperMini)".
Always use the matching file: with a wrong one the readings are wrong, and on PCB v3 an output can drive a pin that is tied to the supply (it can damage the module).

## 2. First start – WiFi setup
1. The probe opens a WiFi network **plant-sensor-xxxxxx**, password **plantsensor**
   (3 fast flashes = service mode).
2. Connect your phone; the setup page opens (otherwise http://192.168.4.1). Choose your WiFi, Save.
3. The probe restarts on your WiFi, still in service mode.
   During this first setup only WiFi is on (no Bluetooth). Can't join the network? Forget it on
   the phone and connect again, type the password exactly, and turn off "mobile data" for a moment
   if the phone keeps dropping a network "without internet".

## 3. Settings – on the web page OR the phone page
| Setting | Web page (service mode) | Phone page (Bluetooth) |
| --- | --- | --- |
| WiFi network + password | group "WiFi": new SSID + password → "Apply WiFi settings (restart)" | WiFi → Save and restart |
| MQTT broker, port, user, password | group "MQTT" → "Apply MQTT settings (restart)" | MQTT → Save and restart (empty password = unchanged) |
| Measure every (min) | "Log interval" | Measuring → Save |
| Send to MQTT every (readings) | "Report every (logs)" | Measuring → Save |
| Service mode timeout | "Service mode timeout" | Measuring → Save |
| Calibration | "Set dry / Set wet (current reading)" | Set dry / Set wet |
| Web page login | Settings: "Web page user / password" → "Apply web login (restart)" | Web page login → Save and restart |

Web page: http://plant-sensor-xxxxxx.local/ or the probe's IP. Login: `web_username` / `web_password`
from secrets.yaml until you set your own. Forgot it? Set a new one from the phone page.
MQTT passwords are never shown or sent back; they stay on the probe and out of Home Assistant.
Bluetooth is on only for 2 minutes after RST and in service mode, so settings can only be changed
by someone holding the probe.

## 4. Normal mode
- Every "measure" interval (60 min) the probe wakes ~0.5 s, measures and stores the reading.
- Every "report" count (6) it also sends to MQTT and sets its clock from the internet.
- **RST**: blink code, send, **Bluetooth on for 2 minutes** (stays on while a phone is connected).

## 5. BOOT button (while awake: after RST or in service mode)
- **Hold 2 s**: LED off + one blink → release = service mode on / off.
- **Hold 5 s**: two blinks → release = **power off**. No wakes at all (a few µA, a cell lasts years).
  Press **RST** to start it again – it starts in service mode. Also on the web page (Settings) and phone page (More).
- **Hold 10 s**: fast blinking = **network reset** (WiFi + MQTT erased → back to step 2).
  Calibration, settings and the log are kept.
- Never hold BOOT while pressing RST (USB download mode).

## 6. Phone page
Web Bluetooth needs https. Free: GitHub Pages
1. github.com → New repository (e.g. `plant-sensor`), Public.
2. Add file → Upload files → drag **all 6 files** from `docs/` (index.html, sw.js, manifest.webmanifest, icon.svg, icon-192.png, icon-512.png) → Commit. To update, upload them again (replace).
3. Settings → Pages → Branch `main`, `/ (root)` → Save → `https://<user>.github.io/plant-sensor/`.
4. Open in **Chrome on Android** (iPhone: **Bluefy**), add to home screen. PL/EN switch top right.
Use: RST on the probe → **Connect** → pick plant-sensor-xxxxxx (with a hub: connect to plant-hub-xxxxxx any time, see section 7). The page sets the probe's clock,
downloads new readings and keeps the full history in the phone.

## Log
128 KB = 16 384 readings (~22 months at 1 per hour); when full the oldest 512 are dropped.

## 7. Hub (optional) – automatic collection
A second ESP32-C3 (e.g. SuperMini) powered from USB all the time. At every reading the probes send new
readings to it over ESP-NOW (no router, ~0.1–0.3 s radio time); the phone connects to the hub over
Bluetooth whenever you want – no RST needed.

- Flash: `python -m esphome run plant_hub.yaml` (keep `hub_partitions.csv` next to it – 448 KB history).
- First start: AP `plant-hub-xxxxxx`, password `planthub`; choose your WiFi. WiFi is optional – without it the hub works offline (Bluetooth only, radio channel 1).
- Probes (0.9.9+): switch "Send to hub (ESP-NOW)" is on by default. Pairing is automatic (channel scan 1–13 after RST and every 6th reading while no hub; again after 3 missed answers). Unsent readings are kept and sent later (up to 200 per wake). The probe takes its clock from the hub.
- Phone + hub: probe list, incremental download, per-probe charts, "Settings via the hub" (name, interval, MQTT every N, service timeout, dry/wet from the last reading, erase probe log). Changes wait in the hub (⏳) and the probe applies them at its next reading.
- MQTT/Home Assistant: configure in the hub; it publishes every probe via HA discovery (`<hub>/<probe>/state`).
- Memory: 28,672 readings in flash; power loss does not erase history or the probe list.
- LED (GPIO8) blinks per received packet. BOOT held 10 s resets the hub's WiFi/MQTT settings (history kept).

## Not yet tested on hardware
Probe 0.9.9 and hub 0.1 compiled here (probe flash 78 %, hub 79 %); the radio protocol passed a simulated test. The hub has not run on real hardware yet.

## Report confirmation (MQTT echo)
Each report is one retained JSON message on `plant-sensor-xxxxxx/report`:
`{"seq":123,"time":"2026-11-04 10:00:00","t":1793786400,"moisture":41.2,"raw":1.874,"battery":3.28,"spread":0.004,"log":512,"failed":0}`.
The probe subscribes to that topic and stays awake until the broker sends the same `seq` back
(up to 3 tries, 4 s each). `seq` changes every time, so an echo proves the message really reached
the broker. `time` shows whether the probe's clock is right. If no echo arrives, the
"Unconfirmed reports" counter goes up. In HA you can read the JSON with an MQTT sensor
and `value_json.moisture`.

## Measurement settings (Diagnostics / Config)
| Setting | Default | Notes |
| --- | --- | --- |
| Probe settle time | 100 ms | The detector settles in < 25 ms (simulated); longer only costs battery |
| Probe drive frequency | 1000 kHz | Lower = more sensitive in wet soil but more affected by salt/fertilizer |
| Probe drive duty | 40 % | Keeps the dry reading below ~2.5 V, where the ESP32-C3 ADC is still accurate |
| Soil raw noise | – | Spread of the 32 samples; should be a few mV. Large = WiFi/power noise or a loose probe |
After changing frequency or duty, set the dry and wet calibration again.
