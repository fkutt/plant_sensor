# Czujnik wilgotności gleby (plant sensor)

Tania bateryjna sonda pojemnościowa do doniczek: ESP32-C3 SuperMini na podstawkach, ogniwo
LiFePO4 14500, elektrody wytrawione w PCB. Pomiar co godzinę, historia w sondzie, telefon przez
Bluetooth, opcjonalnie baza (drugi ESP32-C3 na USB) i Home Assistant przez MQTT.

**Aplikacja na telefon:** strona z folderu `docs/`, publikowana przez GitHub Pages
(adres w opisie repozytorium → *About*). Chrome na Androidzie / PC, na iPhonie przeglądarka Bluefy.

## Zawartość

| Folder | Co jest w środku |
| --- | --- |
| [`docs/`](docs) | Aplikacja na telefon (PWA, działa offline): kafelki czujników, wykresy, ustawienia sond i bazy |
| [`firmware/`](firmware) | Firmware ESPHome: sonda (`plant_sensor_universal*.yaml`) i baza (`plant_hub.yaml`) – opis w [`firmware/README_PL.md`](firmware/README_PL.md) |
| [`hardware/pcb_v2/`](hardware/pcb_v2) | Projekt PCB v2 w KiCad, model 3D płytki, pliki produkcyjne JLCPCB w `production/` |
| [`hardware/enclosure/`](hardware/enclosure) | Obudowa: model STEP i części do druku (STL) |
| [`manual/`](manual) | [Instrukcja obsługi](manual/instrukcja.md) z rysunkami |

## Szybki start

1. **PCB:** zamów płytki z `hardware/pcb_v2/production/` (instrukcja w `ORDER_JLCPCB.md`).
2. **Obudowa:** wydrukuj `plant_sensor_enc_top.stl` i `plant_sensor_enc_bottom.stl`.
3. **Firmware:** skopiuj `firmware/secrets.example.yaml` jako `firmware/secrets.yaml`, wpisz hasła, potem
   `python -m esphome run plant_sensor_universal_pl.yaml` (sonda) albo `python -m esphome run plant_hub.yaml` (baza).
4. **Telefon:** otwórz stronę aplikacji, *Połącz z bazą* (albo *Ustawienia → Sonda bez bazy*), zainstaluj jako aplikację.

## Wersje

| Element | Wersja |
| --- | --- |
| Firmware sondy | 0.9.11 alpha |
| Firmware bazy | 0.1.1 alpha |
| PCB | v2 (SuperMini na podstawkach) |

Plik `firmware/secrets.yaml` z hasłami jest wykluczony z repozytorium (`.gitignore`) – nie wrzucaj go.
