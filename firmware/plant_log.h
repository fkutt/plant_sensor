// Measurement log for the plant sensor: a ring buffer of 8-byte records in its own flash
// partition ("plog", 128 KB = 16384 records = ~22 months at one record per hour).
// Records are appended one by one; a 4 KB sector is erased only when the ring wraps into it,
// so flash wear is negligible. The write position is cached in RTC memory across deep sleep
// and found again by scanning the partition after a reset / battery change.
#pragma once
#define PLANT_LOG_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <sys/time.h>
#include "esp_attr.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "soc/soc_caps.h"

namespace plog {

struct Rec {
  uint32_t t;        // unix time (UTC), 0 = time unknown
  uint16_t moist10;  // soil moisture in 0.1 %
  uint16_t vbat_mv;  // battery voltage in mV
};
static_assert(sizeof(Rec) == 8, "record must be 8 bytes");

static const uint32_t SECTOR = 4096;
static const uint32_t PER_SECTOR = SECTOR / sizeof(Rec);
static const uint32_t MAGIC = 0x504C4F47;  // "PLOG"

static const esp_partition_t *part = nullptr;
static uint32_t cap = 0;    // capacity in records
static uint32_t head = 0;   // next slot to write
static uint32_t count = 0;  // valid records (the oldest is at head - count)

static std::string web_user_s, web_pass_s;  // web page login set at runtime (must outlive the server)

RTC_DATA_ATTR static uint32_t rtc_magic = 0;
RTC_DATA_ATTR static uint32_t rtc_head = 0;
RTC_DATA_ATTR static uint32_t rtc_count = 0;
RTC_DATA_ATTR static uint32_t rtc_wakes = 0;  // timer wakes since the last reset (report counter)

// boot history: the last 10 starts. Kept in RTC memory (survives deep sleep) and copied to flash
// (survives RST, power off and a battery change), so a wake that never finished is still visible.
struct Hist {
  uint32_t magic;
  uint8_t reason[10];   // esp_reset_reason()
  char out[10];         // r report confirmed, f not confirmed, n no MQTT, - nothing to send,
                        // o power off, ? never reached sleep (crash / hang / no wake)
  uint16_t minutes[10]; // sleep chosen at the end of that wake
  uint32_t t[10];       // unix time of the start (0 = clock not set)
};
RTC_NOINIT_ATTR static Hist H;
static esphome::ESPPreferenceObject hist_pref;
static bool hist_pref_ok = false;

inline const char *reason_name(int r) {
  switch (r) {
    case ESP_RST_POWERON: return "PWR";      // power on, battery inserted or RST button
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";            // restart (settings, OTA)
    case ESP_RST_PANIC: return "CRASH";
    case ESP_RST_INT_WDT: return "IWDT";
    case ESP_RST_TASK_WDT: return "TWDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "SLEEP";  // normal timer wake
    case ESP_RST_BROWNOUT: return "LOWV";    // supply dropped (battery / contact / WiFi current)
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    default: return "?";
  }
}
inline void hist_save() {
  if (hist_pref_ok) { hist_pref.save(&H); esphome::global_preferences->sync(); }
}
inline void record_boot() {
  if (!hist_pref_ok) {
    hist_pref = esphome::global_preferences->make_preference<Hist>(0x504C4248UL, true);
    hist_pref_ok = true;
  }
  if (H.magic != MAGIC) {                  // RTC memory lost (RST / power): take the flash copy
    Hist f;
    if (hist_pref.load(&f) && f.magic == MAGIC) H = f;
    else { memset(&H, 0, sizeof(H)); memset(H.reason, 0xFF, sizeof(H.reason)); H.magic = MAGIC; }
  }
  memmove(H.reason + 1, H.reason, sizeof(H.reason) - 1);
  memmove(H.out + 1, H.out, sizeof(H.out) - 1);
  memmove(H.minutes + 1, H.minutes, sizeof(H.minutes) - sizeof(H.minutes[0]));
  memmove(H.t + 1, H.t, sizeof(H.t) - sizeof(H.t[0]));
  H.reason[0] = (uint8_t) esp_reset_reason();
  H.out[0] = '?';
  H.minutes[0] = 0;
  time_t now = ::time(nullptr);
  H.t[0] = now > 1700000000 ? (uint32_t) now : 0;
  hist_save();
}
inline void note_result(char c) { H.out[0] = c; }
inline void note_sleep(uint16_t minutes) {
  H.minutes[0] = minutes;
  if (H.out[0] == '?') H.out[0] = '-';
  hist_save();
}
inline std::string boot_history() {   // newest first: "SLEEP 20:35, SLEEP 20:25 r 10, ..."
  std::string s;
  char b[40], hm[8];
  for (size_t i = 0; i < sizeof(H.reason) && H.reason[i] != 0xFF; i++) {
    if (H.t[i]) { time_t tt = H.t[i]; struct tm tmv; localtime_r(&tt, &tmv); strftime(hm, sizeof(hm), "%H:%M", &tmv); }
    else snprintf(hm, sizeof(hm), "--:--");
    char o = (H.out[i] >= 32 && H.out[i] < 127) ? H.out[i] : '?';
    if (i == 0) snprintf(b, sizeof(b), "%s %s", reason_name(H.reason[i]), hm);   // this wake: still running
    else snprintf(b, sizeof(b), ", %s %s %c %u", reason_name(H.reason[i]), hm, o, (unsigned) H.minutes[i]);
    s += b;
  }
  return s;
}

// Strapping pins GPIO2 / GPIO8 / GPIO9 must read high when the chip restarts after deep sleep.
// Without an external pull-up GPIO2 floats in sleep and may read low -> the chip does not boot.
// Keep their internal pull-ups on (held) through deep sleep, release the hold after the wake.
static const gpio_num_t STRAPS[] = {GPIO_NUM_2, GPIO_NUM_8, GPIO_NUM_9};
inline void hold_straps() {
  for (gpio_num_t g : STRAPS) {
    gpio_pullup_en(g);
    gpio_pulldown_dis(g);
    gpio_hold_en(g);
  }
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
  gpio_deep_sleep_hold_en();
#endif
}
inline void release_straps() {
  for (gpio_num_t g : STRAPS) gpio_hold_dis(g);
#if !SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP
  gpio_deep_sleep_hold_dis();
#endif
}

inline bool is_erased(const Rec &r) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&r);
  for (size_t i = 0; i < sizeof(Rec); i++)
    if (p[i] != 0xFF) return false;
  return true;
}

inline void save_rtc() {
  rtc_head = head;
  rtc_count = count;
  rtc_magic = MAGIC;
}

// find the partition; restore the position from RTC memory or by scanning the flash
inline bool init() {
  if (part != nullptr) return true;
  part = esp_partition_find_first(static_cast<esp_partition_type_t>(0x40),
                                  static_cast<esp_partition_subtype_t>(0x00), "plog");
  if (part == nullptr) return false;
  cap = part->size / sizeof(Rec);
  if (rtc_magic == MAGIC && rtc_head < cap && rtc_count <= cap) {
    head = rtc_head;
    count = rtc_count;
    return true;
  }
  // full scan: count written slots, head = first erased slot after a written one
  static Rec buf[PER_SECTOR];
  Rec last{};
  esp_partition_read(part, (cap - 1) * sizeof(Rec), &last, sizeof(Rec));
  bool prev_erased = is_erased(last);
  bool found = false;
  count = 0;
  head = 0;
  for (uint32_t s = 0; s < cap / PER_SECTOR; s++) {
    esp_partition_read(part, s * SECTOR, buf, SECTOR);
    for (uint32_t i = 0; i < PER_SECTOR; i++) {
      bool er = is_erased(buf[i]);
      if (!er) count++;
      if (er && !prev_erased && !found) {
        head = s * PER_SECTOR + i;
        found = true;
      }
      prev_erased = er;
    }
  }
  if (count == 0) head = 0;
  if (count >= cap) {  // should not happen (one sector is always kept erased): start over
    esp_partition_erase_range(part, 0, part->size);
    head = 0;
    count = 0;
  }
  save_rtc();
  return true;
}

inline void append(const Rec &r) {
  if (!init()) return;
  if (head % PER_SECTOR == 0) {  // entering a sector: erase it (drops the oldest records)
    esp_partition_erase_range(part, head * sizeof(Rec), SECTOR);
    if (count > cap - PER_SECTOR) count = cap - PER_SECTOR;
  }
  esp_partition_write(part, head * sizeof(Rec), &r, sizeof(Rec));
  head = (head + 1) % cap;
  count++;
  save_rtc();
}

// i = 0 is the oldest record
inline bool get(uint32_t i, Rec &r) {
  if (!init() || i >= count) return false;
  uint32_t slot = (head + cap - count + i) % cap;
  return esp_partition_read(part, slot * sizeof(Rec), &r, sizeof(Rec)) == ESP_OK;
}

inline void clear() {
  if (!init()) return;
  esp_partition_erase_range(part, 0, part->size);
  head = 0;
  count = 0;
  save_rtc();
}

inline uint32_t size() { return init() ? count : 0; }
inline uint32_t capacity() { init(); return cap; }

inline uint32_t now_or_zero() {
  time_t now = ::time(nullptr);
  return now > 1700000000 ? static_cast<uint32_t>(now) : 0;
}

inline void set_time(uint32_t unix_s) {
  struct timeval tv;
  tv.tv_sec = unix_s;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

// BLE chunk: [start u32][total u32][n u8] + n records (max 60 -> 489 bytes)
inline std::vector<uint8_t> chunk(uint32_t start) {
  std::vector<uint8_t> out;
  uint32_t total = size();
  uint8_t n = 0;
  out.resize(9);
  memcpy(&out[0], &start, 4);
  memcpy(&out[4], &total, 4);
  for (uint32_t i = start; i < total && n < 60; i++, n++) {
    Rec r;
    if (!get(i, r)) break;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&r);
    out.insert(out.end(), p, p + sizeof(Rec));
  }
  out[8] = n;
  return out;
}

}  // namespace plog
