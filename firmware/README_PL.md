# Czujnik roślin – firmware uniwersalny

Jeden firmware dla wszystkich sond. Bez osobnych plików dla każdej sondy i bez ustawień WiFi/MQTT w kodzie.

| Plik | Co to jest |
| --- | --- |
| `plant_sensor_universal_pl.yaml` | **Ten wgrywasz** – sonda bez czujników, nazwy po polsku |
| `plant_sensor_universal_env_pl.yaml` | **Ten wgrywasz** – sonda z SHT40 / LTR-303 / SCD41, po polsku |
| `plant_sensor_universal.yaml` / `_env.yaml` | To samo, nazwy po angielsku |
| `plant_sensor_base.yaml`, `sensors_env.yaml` | Części wspólne (dołączane automatycznie) |
| `plant_log.h`, `partitions_plant.csv` | Historia pomiarów we własnym obszarze pamięci flash |
| `docs/` | Aplikacja na telefon (instalowana strona, działa offline): historia przez Bluetooth, wykres, kalibracja, ustawienia, kopia zapasowa, PL/EN |
| `secrets.yaml` | Tylko `ota_password`, `web_username`, `web_password` |

Wszystkie pliki trzymaj w jednym folderze. Język zmienia nazwy na stronie sondy i w Home Assistant.
(Ramka strony www i strona konfiguracji WiFi pochodzą z ESPHome i zostają po angielsku.)

## 1. Kompilacja i wgranie (raz na sondę, przez USB)
Po wgraniu przez USB w trybie pobierania ESP32-C3 nie restartuje się sam: naciśnij **RST**.
Zmienia się układ pamięci, więc pierwszy raz trzeba wgrać przez USB:
1. Przytrzymaj BOOT, naciśnij i puść RST, puść BOOT (tryb pobierania).
2. `py -m esphome run plant_sensor_universal_pl.yaml` → wybierz port COM.
Ten sam plik wgrywasz na każdą sondę. Pierwsza kompilacja pobiera ESP-IDF i trwa dłużej.

## Płytka (wersja PCB i moduł)
Każdy plik główny ma jedną linię wybierającą mapę pinów:
`board: !include board_v2.yaml` – zmień ją przed kompilacją:

| Plik | Dla |
| --- | --- |
| `board_v2.yaml` | PCB v2 (obecne płytki), ESP32-C3 SuperMini |
| `board_v3_supermini.yaml` | PCB v3 z SuperMini (JP1/JP3/JP4 na polach „S”, JP2 wlutowany) |
| `board_v3_zero.yaml` | PCB v3 z Waveshare ESP32-C3-Zero (JP1/JP3/JP4 na polach „Z”, JP2 pusty) |

Strona www pokazuje wersję z nazwą płytki, np. „0.9.3 alpha (v2 SuperMini)”.
Zawsze używaj właściwego pliku: przy złym odczyty są błędne, a na PCB v3 wyjście może sterować pinem połączonym z zasilaniem (może to uszkodzić moduł).

## 2. Pierwsze uruchomienie – konfiguracja WiFi
1. Sonda tworzy sieć WiFi **plant-sensor-xxxxxx**, hasło **plantsensor** (3 szybkie błyski = tryb serwisowy).
2. Połącz telefon; strona konfiguracji otworzy się sama (albo wejdź na http://192.168.4.1).
   Wybierz swoją sieć WiFi, wpisz hasło, zapisz.
3. Sonda restartuje się w Twojej sieci, nadal w trybie serwisowym.
   Podczas pierwszej konfiguracji działa tylko WiFi (bez Bluetooth). Nie możesz się połączyć?
   Usuń sieć w telefonie i połącz ponownie, wpisz dokładnie hasło, na chwilę wyłącz dane komórkowe,
   jeśli telefon rozłącza sieć „bez dostępu do internetu”.

## 3. Ustawienia – na stronie www sondy LUB na telefonie
| Ustawienie | Strona www (tryb serwisowy) | Telefon (Bluetooth) |
| --- | --- | --- |
| Sieć WiFi + hasło | grupa „WiFi” → „Zapisz WiFi (restart)” | WiFi → Zapisz i zrestartuj |
| Broker MQTT, port, użytkownik, hasło | grupa „MQTT” → „Zapisz ustawienia MQTT (restart)” | MQTT → Zapisz i zrestartuj (puste hasło = bez zmian) |
| Pomiar co (min) | „Interwał pomiaru” | Pomiary → Zapisz |
| Wysyłka do MQTT co (pomiarów) | „Wysyłka co (pomiarów)” | Pomiary → Zapisz |
| Limit trybu serwisowego | „Limit czasu trybu serwisowego” | Pomiary → Zapisz |
| Kalibracja | „Ustaw sucho / Ustaw mokro (bieżący odczyt)” | Ustaw sucho / Ustaw mokro |
| Login strony www | Ustawienia: „Użytkownik / Hasło strony www” → „Zapisz login strony (restart)” | Logowanie do strony www → Zapisz i zrestartuj |

Strona www: http://plant-sensor-xxxxxx.local/ lub adres IP sondy. Login: `web_username` / `web_password`
z secrets.yaml, dopóki nie ustawisz własnego. Zapomniane hasło? Ustaw nowe z telefonu.
Hasła MQTT nie są nigdzie wyświetlane ani wysyłane do Home Assistant.
Bluetooth działa tylko 2 minuty po RST i w trybie serwisowym – ustawienia może zmienić tylko ktoś, kto trzyma sondę w ręku.

## 4. Tryb normalny
- Co „interwał pomiaru” (60 min) sonda budzi się na ~0,5 s, mierzy i zapisuje pomiar.
- Co „wysyłka co” (6) pomiarów łączy się z WiFi, wysyła do MQTT i ustawia zegar z internetu.
- **RST**: kod mrugania, wysyłka, **Bluetooth na 2 minuty** (dłużej, gdy telefon jest połączony).

## 5. Przycisk BOOT (gdy sonda nie śpi: po RST lub w trybie serwisowym)
- **Przytrzymaj 2 s**: dioda gaśnie i mrugnie raz → puść = włącz/wyłącz tryb serwisowy.
- **Przytrzymaj 5 s**: dwa mrugnięcia → puść = **wyłączenie**. Sonda nie budzi się wcale (kilka µA, ogniwo wystarczy na lata).
  **RST** włącza ją ponownie – startuje w trybie serwisowym. Także na stronie www (Ustawienia) i w telefonie (Więcej).
- **Przytrzymaj 10 s**: szybkie miganie = **reset sieci** (WiFi i MQTT kasowane → wracasz do kroku 2).
  Kalibracja, ustawienia i historia zostają.
- Nigdy nie trzymaj BOOT podczas naciskania RST (to tryb pobierania przez USB).

## 6. Strona na telefon
W repozytorium strona leży w folderze `docs/` i jest publikowana przez GitHub Pages (opis w głównym README.md). Poniżej sposób dla osobnego repozytorium.

Web Bluetooth wymaga adresu https. Za darmo: GitHub Pages
1. github.com → New repository (np. `plant-sensor`), Public.
2. Add file → Upload files → przeciągnij **wszystkie 6 plików** z folderu `docs/` w tym repozytorium (index.html, sw.js, manifest.webmanifest, icon.svg, icon-192.png, icon-512.png) → Commit. Przy aktualizacji wgraj je ponownie (zastąp).
3. Settings → Pages → Branch `main`, `/ (root)` → Save → `https://<użytkownik>.github.io/plant-sensor/`.
4. Otwórz w **Chrome na Androidzie** (iPhone: **Bluefy**), dodaj do ekranu głównego. Przełącznik PL/EN w prawym górnym rogu.
Użycie bez bazy: RST na sondzie → **Połącz** → wybierz plant-sensor-xxxxxx.
Z bazą: **Połącz** → wybierz plant-hub-xxxxxx (w dowolnej chwili, bez naciskania RST) – patrz rozdział 7. Strona ustawia zegar sondy,
pobiera nowe pomiary i przechowuje pełną historię w telefonie.

## Historia w sondzie
128 KB = 16 384 pomiary (~22 miesiące przy 1 pomiarze na godzinę); po zapełnieniu najstarsze 512 są usuwane.

## 7. Baza (opcjonalna) – automatyczne zbieranie pomiarów
Drugi ESP32-C3 (np. SuperMini) stale zasilany z USB. Sondy przy każdym pomiarze wysyłają do niej
nowe odczyty przez ESP-NOW (radio bez routera, ok. 0,1–0,3 s), a telefon łączy się z bazą przez
Bluetooth kiedy chcesz – bez naciskania RST.

**Wgranie:** `python -m esphome run plant_hub.yaml` (USB, jak sonda). Plik `hub_partitions.csv`
musi leżeć obok – zostawia 448 KB pamięci na historię.

**Pierwsze uruchomienie:** baza tworzy sieć `plant-hub-xxxxxx`, hasło `planthub`. Wybierz w niej swoją
sieć WiFi (jak przy sondzie). WiFi jest **opcjonalne** – bez niego baza działa offline (tylko Bluetooth).
Bez skonfigurowanego WiFi baza utrzymuje własny kanał radiowy 1; z WiFi używa kanału routera,
a sondy same go znajdą.

**Sondy:** od wersji 0.9.9 przełącznik **„Wysyłka do bazy (ESP-NOW)”** jest domyślnie włączony.
Parowanie jest automatyczne: sonda bez bazy przeszukuje kanały 1–13 (po RST oraz co 6. pomiar),
zapamiętuje pierwszą bazę, która odpowie. Gdy baza nie odpowiada 3 razy z rzędu, sonda szuka jej ponownie.
Pomiary niewysłane (baza wyłączona, poza zasięgiem) czekają w sondzie i zostaną dosłane – do 200 na jedno wybudzenie.
Sonda bierze też zegar z bazy, więc nie trzeba go ustawiać telefonem.
**Sonda z bazą nie używa WiFi ani Bluetooth** (od 0.9.10): gdy baza odpowiada, sonda tylko mierzy,
wysyła do bazy i zasypia – to baza rozmawia z telefonem i z Home Assistant. Po RST: kod wilgotności,
potem **jedno świecenie przez 1 s = dostarczone do bazy** i sonda śpi po ok. 10 s. Gdy baza nie odpowie,
sonda zachowuje się jak dawniej (Bluetooth 2 min, WiFi/MQTT jeśli ustawione). Do naprawy/konfiguracji
sondy zawsze działa tryb serwisowy (BOOT 2 s po RST – WiFi i Bluetooth włączone).

**Telefon z bazą:** strona sama łączy się z ostatnio używaną bazą (też po odświeżeniu), wraca
po utracie połączenia i co minutę dociąga nowe pomiary. Na górze tylko kafelki czujników i wykres;
wszystko inne jest w zwiniętych „Ustawieniach” na dole. Lista sond (nazwa, wilgotność, bateria, kiedy ostatnio, siła sygnału),
pobieranie tylko nowych pomiarów, wykres każdej sondy. Karta **„Ustawienia przez bazę”**: nazwa
(np. roślina), interwał pomiaru, wysyłka MQTT co N, limit trybu serwisowego, kalibracja sucho/mokro
z ostatniego odczytu, kasowanie historii w sondzie. Zmiany zapisują się w bazie (⏳ = czekają)
i sonda odbiera je przy **następnym pomiarze** – baza potwierdza, gdy sonda je zastosowała.

**MQTT/Home Assistant:** w bazie (telefon → Ustawienia bazy albo strona www bazy – login i hasło jak
w `secrets.yaml` (`web_username` / `web_password`), dopóki ich nie zmienisz). Baza publikuje encje każdej sondy przez discovery HA
(`<baza>/<sonda>/state`). Sondy nie muszą wtedy łączyć się z WiFi wcale.

**Pamięć bazy:** 28 672 pomiary w pamięci flash (np. 5 sond × 1/h ≈ 8 miesięcy; potem najstarsze
są nadpisywane). Odłączenie zasilania **nie kasuje** historii ani listy sond. Po zaniku zasilania baza
nie zna godziny do czasu połączenia z WiFi (NTP), telefonem albo sondą, która ma zegar.

**Dioda** (GPIO8) mignie przy każdym odebranym pakiecie. **BOOT trzymany 10 s** = kasuje ustawienia
WiFi/MQTT bazy (historia zostaje).

**Zasięg:** jak WiFi 2,4 GHz, zwykle kilka pokoi; siłę sygnału każdej sondy widać na liście (−85 dBm i gorzej = słabo).
**Bateria:** wysyłka do bazy dodaje ok. 0,1–0,3 s radia na pomiar – dużo mniej niż połączenie z WiFi.

## Jeszcze nie testowane na sprzęcie
Firmware sondy 0.9.9 i bazy 0.1 skompilowały się tutaj (sonda: flash 78 %, baza: flash 79 %),
a protokół radiowy przeszedł test symulacyjny (parowanie, dosyłanie zaległych, zgubione potwierdzenie,
ustawienia, pobieranie, zanik zasilania bazy). Na prawdziwym sprzęcie bazy jeszcze nikt nie uruchamiał.

## Potwierdzenie wysyłki (echo MQTT)
Każdy raport to jedna wiadomość JSON z flagą retained na `plant-sensor-xxxxxx/report`:
`{"seq":123,"time":"2026-11-04 10:00:00","t":1793786400,"moisture":41.2,"raw":1.874,"battery":3.28,"spread":0.004,"log":512,"failed":0}`.
Sonda subskrybuje ten temat i nie zasypia, dopóki broker nie odeśle tego samego `seq`
(do 3 prób po 4 s). `seq` jest za każdym razem inny, więc echo potwierdza, że wiadomość naprawdę
trafiła do brokera. `time` pokazuje, czy zegar sondy jest poprawny. Gdy echo nie przyjdzie,
rośnie licznik „Niepotwierdzone wysyłki”. W HA możesz odczytać JSON sensorem MQTT z `value_json.moisture`.

## Ustawienia pomiaru (Diagnostyka / Konfiguracja)
| Ustawienie | Domyślnie | Uwagi |
| --- | --- | --- |
| Czas stabilizacji sondy | 100 ms | Detektor ustala się w < 25 ms (symulacja); dłużej = tylko większe zużycie baterii |
| Częstotliwość sondy | 1000 kHz | Niższa = czulsza w mokrej glebie, ale bardziej wrażliwa na sól i nawozy |
| Wypełnienie sygnału sondy | 40 % | Odczyt „sucho” poniżej ~2,5 V, gdzie przetwornik ADC ESP32-C3 jest jeszcze dokładny |
| Szum odczytu surowego | – | Rozrzut 32 próbek; powinien wynosić kilka mV. Duży = zakłócenia WiFi/zasilania lub luźna sonda |
Po zmianie częstotliwości lub wypełnienia skalibruj ponownie sucho i mokro.
