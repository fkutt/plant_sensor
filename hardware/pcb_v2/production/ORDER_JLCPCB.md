# Ordering at JLCPCB – plant sensor v2 (SuperMini)

Upload these three files:
1. `plant_sensor_gerbers_JLCPCB.zip` -> PCB order (Gerbers + PTH/NPTH drill files)
2. `plant_sensor_BOM_JLCPCB.csv` -> assembly: Bill of Materials
3. `plant_sensor_CPL_JLCPCB.csv` -> assembly: Pick & Place (CPL)

## PCB options
- Base material FR-4, 2 layers; size should read **25.1 x 181.1 mm**
- PCB thickness 1.6 mm, any colour except white
- Surface finish: HASL (lead-free if you want RoHS). OSP is fine if you solder the hand parts within a few weeks.
- Mark on PCB: "Remove Mark"
- Everything else at default

## PCB Assembly
- Tick "PCB Assembly", type **Economic**, assembly side **Top**, quantity = PCB quantity
- Edge rails / tooling holes: let JLCPCB add them if it asks
- BOM: 12 lines, 26 parts per board, all with LCSC numbers. Two are "extended" parts
  (small feeder fee each): U1 BQ25170DSGR and D1 BAT54WS.
- Parts that are in the design but deliberately **not** in the BOM/CPL (you fit them, or they are copper):
  U2 SuperMini (sockets), BT1 battery holder, J2 JST connector, U3 SHT40, U4 LTR-303, U5 SCD41,
  U6 RTC, H1 hole, E1 probe electrodes
- **Check the placement preview** and rotate any part that sits wrong. Pay attention to:
  U1 (pin-1 dot), Q1 (SOT-23: one pin on one side, two on the other, as the silkscreen shows),
  D1 and LED1 (cathode bar on the silkscreen)

## Buy separately (LCSC / AliExpress) and fit by hand
| Part | Qty per board | Notes |
| --- | --- | --- |
| ESP32-C3 SuperMini | 1 | remove its red power LED and 3.3 V regulator before use |
| 1x8 female socket 2.54 mm | 2 | back side; male pins on the SuperMini |
| Keystone 2460 (or same-footprint AA holder) | 1 | back side, + at the probe end |
| LiFePO4 14500 cell 3.2 V | 1 | never Li-ion 3.7/4.2 V |
| JST S4B-PH-K-S (optional) | 1 | back side, opening out over the right edge |
| SHT40-AD1B (optional) | 1 | hot air |
| LTR-303ALS-01 (optional) | 1 | hot air |
| SCD41 (optional) | 1 | hot air |
| RV-8523-C3 (optional) | 1 | hot air |

## Order of hand assembly
1. Optional sensors / RTC on the front (hot air) – before the sockets and holder are on.
2. Sockets and JST on the back, then the battery holder.
3. Prepare the SuperMini (LED + regulator off), plug it in, insert the cell, flash over USB-C.
