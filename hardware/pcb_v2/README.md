# Plant sensor v2 (SuperMini) – KiCad project

LiFePO4 14500 soil-moisture sensor with a plug-in ESP32-C3 SuperMini, BQ25170 charger set to
3.50 V, capacitive probe etched into the PCB, and footprints for optional air sensors.

Open `plant_sensor.kicad_pro` in KiCad 7, 8 or 9 (files are KiCad 7 format; newer versions
upgrade them when you save).

**Status: built and working (v2).** Production files for JLCPCB are in `production/` (see `production/ORDER_JLCPCB.md`). The 3D model of the assembled board: `plant_sensor_v2_assembly.step`.

## What is in the project

| File | Contents |
| --- | --- |
| `plant_sensor.kicad_sch` | Full schematic, one A3 sheet. Nets are joined by labels. Optional sensors are marked DNP. |
| `plant_sensor.kicad_pcb` | 2-layer board, 25 mm head / 20 mm stake, 181 mm long, routed, GND pours filled, DRC clean. |
| `plant_sensor.kicad_sym` | Project symbols: ESP32-C3 SuperMini, BQ25170DSG, probe electrodes. |
| `plant_sensor.pretty/` | Project footprints: SuperMini on sockets, 3.5 mm hole, probe electrodes. |
| `plant_sensor_BOM.csv` | Full parts list with what JLCPCB places and what you fit by hand. |

## Board

- **Back:** the Keystone 2460 holder for the 14500 cell, pushed against the left edge, and the
  ESP32-C3 SuperMini lying sideways above it on two 1x8 2.54 mm female sockets (detachable).
  USB-C sticks out ~1 mm at the left edge, the antenna end is at the right over a copper-free area
  (no pours or vias, both layers). Right of the holder: the optional JST-PH 4-pin side-entry
  connector J2 for an external I2C sensor; its mating face sticks out 2.0 mm past the right edge
  (flush with a 2 mm enclosure wall).
- **Front:** charger, battery divider and probe detector (placed by JLCPCB), plus the sensor area:
  SHT40 and LTR-303 at the top, SCD41 below them. The sensors themselves are hand-soldered only on the probes that need them;
  their power switch, pull-ups and capacitors are placed on every board.
- Head 25 mm wide with 1 mm top corners, all the way down to the soil line, where a 45° step goes to
  the 20 mm stake. Soil line at 83 mm, probe 98 mm, 2 mm fillets into the 45° bullet tip.
- One 3.5 mm enclosure hole right of the battery + pin (x = +8.25 mm); its edge is 5.0 mm above the
  soil line, leaving a band for the enclosure seal.
- Probe: 8 mm signal electrode in the middle, 3 mm ground horseshoe around it, both layers,
  under solder mask. Detector R13/D1/C11/R14 just above the 4 mm seal band.
- **Seal band:** 4 mm above the soil line (y 79–83 mm) is free of components on both sides (a DRC
  rule area enforces it); only tracks and ground copper cross it, under solder mask.

## SuperMini pinout used (check against your board!)

Seen from the SuperMini's component side, USB-C up:

| Right column (top→bottom) | Use | Left column (top→bottom) | Use |
| --- | --- | --- | --- |
| 5V | → charger input (VBUS) | GPIO5 | divider enable |
| GND | GND | GPIO6 | – |
| 3V3 | ← battery (VBAT) | GPIO7 | SDA |
| GPIO4 | probe drive (PWM) | GPIO8 | blue LED = status |
| GPIO3 | sensor power (low = on) | GPIO9 | BOOT button |
| GPIO2 | – | GPIO10 | SCL |
| GPIO1 | battery / 2 (ADC) | GPIO20 | – |
| GPIO0 | moisture (ADC) | GPIO21 | – |

Rows 15.24 mm apart, pins 2.54 mm. **Measure your SuperMini before ordering**: row-to-row distance
and that the pins sit centred along the 22.5 mm length.

## Preparing each SuperMini (hot air, 1 minute)

1. Remove the **red power LED** (always on, a few mA).
2. Remove the **3.3 V regulator** (5-pin chip next to USB-C). Reasons: it would push 3.3 V straight
   into the battery when USB is plugged in (uncontrolled charging, regulator gets hot), it leaks
   battery current backwards when unplugged, and it costs ~40 µA in sleep.
   Afterwards the battery powers the ESP32 and USB-C only charges and flashes, so **flash with the
   battery inserted**.

## Controls

- **RST** on the SuperMini = WAKE: measure now, blink the moisture code on the blue LED, send, sleep.
  Slow blinking = dry, fast = wet; a long 1.5 s glow first = battery low.
- **BOOT** on the SuperMini: hold it, press RST, release → download mode for USB flashing.
- Red **CHG** LED (front) lights while charging.

## Key values

| Ref | Value | Why |
| --- | --- | --- |
| R4 | 100 kΩ | BQ25170 VSET → LiFePO4 3.50 V |
| R3 | 1 kΩ | BQ25170 ISET → about 300 mA |
| R5 | 10 kΩ | BQ25170 TS without thermistor |
| R13 | 10 kΩ | Probe drive resistor; tune 4.7–47 kΩ for best dry/wet span |
| C11 / R14 | 100 nF / 1 MΩ | Peak detector hold and discharge |
| R16 / R17 | 100 kΩ / 100 kΩ | Battery divider; bottom goes to GPIO5, so no current while asleep |
| Q1 / R21 | AO3401A / 100 kΩ | Sensor supply switch; off in sleep |
| R19 / R20 | 4.7 kΩ | I2C pull-ups (on the switched supply) |

## Optional sensors (hand-soldered)

| Ref | Part | Measures | ESPHome |
| --- | --- | --- | --- |
| U3 | SHT40-AD1B | air temperature, humidity | `sht4x` |
| U4 | LTR-303ALS-01 | light, lux (0.01–64k) | `ltr_als_ps` |
| U5 | SCD41 | CO2 (single-shot, 5 s) | `scd4x` |
| U6 | RV-8523-C3 | real-time clock (crystal built in, ~150 nA, ±20 ppm), always on the battery | custom firmware (PCF8523-compatible) |
| J2 | JST S4B-PH-K-S (2.0 mm, side entry) | external I2C sensor; pins 1-4 = SCL SDA 3V3 GND (Grove order) | – |

Never fit a Li-ion 14500 (4.2 V): the cell connects straight to the ESP32-C3.


The current firmware is in [`../../firmware`](../../firmware) (pick `board: !include board_v2.yaml`).
