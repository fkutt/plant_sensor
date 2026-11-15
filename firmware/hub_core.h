// Plant sensor HUB: receives the probes' readings over ESP-NOW, keeps them in its own flash
// area ("hlog", survives power loss), answers each probe with the clock and pending settings,
// serves everything to the phone page over Bluetooth and (optionally) to Home Assistant via MQTT.
#pragma once
#define PNET_HUB
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "esp_partition.h"
#include "esp_wifi.h"
#include "plant_net.h"

namespace hub {

using pnet::TIME_VALID;

// ------------------------------------------------------------------ measurement log (ring in "hlog")
struct __attribute__((packed)) HRec {
  uint32_t t;        // unix seconds, 0 = unknown
  uint32_t serial;   // running number, never repeats (phone downloads "everything after serial X")
  uint16_t moist10;
  uint16_t vbat_mv;
  uint16_t raw_mv;
  uint8_t probe;     // index in the probe table
  int8_t rssi;
};
static_assert(sizeof(HRec) == 16, "HRec size");

static const uint32_t SECTOR = 4096;
static const uint32_t PER_SECTOR = SECTOR / sizeof(HRec);
static const esp_partition_t *part = nullptr;
static uint32_t cap = 0, head = 0, count = 0, last_serial = 0;

inline bool erased(const HRec &r) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(&r);
  for (size_t i = 0; i < sizeof(r); i++) if (p[i] != 0xFF) return false;
  return true;
}

inline bool log_init() {
  if (part) return true;
  part = esp_partition_find_first(static_cast<esp_partition_type_t>(0x40), static_cast<esp_partition_subtype_t>(0x00), "hlog");
  if (!part) return false;
  cap = part->size / sizeof(HRec);
  // scan: the newest record has the highest serial; head = slot after it
  static HRec buf[PER_SECTOR];
  uint32_t best = 0, best_slot = 0;
  bool any = false;
  count = 0;
  for (uint32_t s = 0; s < cap / PER_SECTOR; s++) {
    esp_partition_read(part, s * SECTOR, buf, SECTOR);
    for (uint32_t i = 0; i < PER_SECTOR; i++) {
      if (erased(buf[i])) continue;
      count++;
      if (!any || buf[i].serial > best) { best = buf[i].serial; best_slot = s * PER_SECTOR + i; any = true; }
    }
  }
  last_serial = any ? best : 0;
  head = any ? (best_slot + 1) % cap : 0;
  if (count >= cap) count = cap - PER_SECTOR;
  return true;
}

inline void log_append(HRec r) {
  if (!log_init()) return;
  if (head % PER_SECTOR == 0) {
    esp_partition_erase_range(part, head * sizeof(HRec), SECTOR);
    if (count > cap - PER_SECTOR) count = cap - PER_SECTOR;
  }
  r.serial = ++last_serial;
  esp_partition_write(part, head * sizeof(HRec), &r, sizeof(r));
  head = (head + 1) % cap;
  count++;
}

inline bool log_get(uint32_t i, HRec &r) {   // i = 0 oldest
  if (!log_init() || i >= count) return false;
  uint32_t slot = (head + cap - count + i) % cap;
  return esp_partition_read(part, slot * sizeof(HRec), &r, sizeof(r)) == ESP_OK;
}

inline void log_clear() {
  if (!log_init()) return;
  esp_partition_erase_range(part, 0, part->size);
  head = 0; count = 0;   // serials keep counting up, so phones never mix old and new
}

inline uint32_t oldest_serial() { HRec r; return log_get(0, r) ? r.serial : last_serial + 1; }

// BLE chunk: [first serial u32][newest serial u32][n u8] + n x HRec (max 30 -> 489 bytes)
inline std::vector<uint8_t> chunk(uint32_t from_serial) {
  std::vector<uint8_t> out(9);
  uint32_t s0 = oldest_serial();
  uint32_t i = from_serial > s0 ? from_serial - s0 : 0;
  uint8_t n = 0;
  uint32_t first = 0;
  for (; i < count && n < 30; i++, n++) {
    HRec r;
    if (!log_get(i, r)) break;
    if (n == 0) first = r.serial;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&r);
    out.insert(out.end(), p, p + sizeof(r));
  }
  memcpy(&out[0], &first, 4);
  memcpy(&out[4], &last_serial, 4);
  out[8] = n;
  return out;
}

// ------------------------------------------------------------------ probe table (kept in preferences)
static const int MAXP = 24;
struct __attribute__((packed)) PInfo {
  uint8_t mac[6];
  uint8_t used;
  uint8_t cfg_ver;           // settings version the probe reported
  uint32_t seen;             // unix (or 0)
  uint32_t seen_up;          // hub uptime (s) at last contact
  uint16_t vbat_mv, moist10, raw_mv, li, dry_mv, wet_mv;
  uint8_t re, st;
  int8_t rssi;
  uint8_t pad;
  uint16_t unsent;
  char fw[8];
  uint32_t last_raw_t;       // newest record time stored (probe clock) - drops duplicates
  uint32_t last_probe_now;
  char label[20];            // name given in the phone app ("Monstera")
  // settings waiting for the probe's next contact
  uint8_t p_active, p_ver, p_cmd, p_re, p_st, pad2;
  uint16_t p_li, p_dry, p_wet;
};
struct Table { uint32_t magic; PInfo p[MAXP]; };
static Table T;
static esphome::ESPPreferenceObject pref;
static bool dirty = false;
static bool mqtt_dirty[MAXP] = {false};
static bool mqtt_disc[MAXP] = {false};
static esphome::espnow::ESPNowComponent *radio = nullptr;
static uint32_t rx_count = 0;
static int last_rx_probe = -1;
static bool led_pulse = false;
static std::string web_user_s, web_pass_s;

inline void table_load() {
  pref = esphome::global_preferences->make_preference<Table>(0x48554254UL, true);
  if (!pref.load(&T) || T.magic != 0x48554254UL) { memset(&T, 0, sizeof(T)); T.magic = 0x48554254UL; }
}
inline void table_save() { if (dirty) { pref.save(&T); dirty = false; } }

inline std::string probe_name(const uint8_t *mac) {
  char b[24];
  snprintf(b, sizeof(b), "plant-sensor-%02x%02x%02x", mac[3], mac[4], mac[5]);
  return b;
}
inline int find_probe(const uint8_t *mac, bool create) {
  int free_i = -1;
  for (int i = 0; i < MAXP; i++) {
    if (T.p[i].used && memcmp(T.p[i].mac, mac, 6) == 0) return i;
    if (!T.p[i].used && free_i < 0) free_i = i;
  }
  if (!create) return -1;
  if (free_i < 0) {                     // table full: reuse the probe not heard from longest
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < MAXP; i++) if (T.p[i].seen_up < oldest) { oldest = T.p[i].seen_up; free_i = i; }
  }
  memset(&T.p[free_i], 0, sizeof(PInfo));
  memcpy(T.p[free_i].mac, mac, 6);
  T.p[free_i].used = 1;
  dirty = true;
  return free_i;
}
inline int probe_count() { int n = 0; for (int i = 0; i < MAXP; i++) n += T.p[i].used ? 1 : 0; return n; }

inline uint8_t channel() { uint8_t ch = 1; wifi_second_chan_t s; esp_wifi_get_channel(&ch, &s); return ch; }
inline uint32_t uptime_s() { return (uint32_t) (esp_timer_get_time() / 1000000); }

// ------------------------------------------------------------------ packet from a probe
inline void on_packet(const uint8_t *src, const uint8_t *data, uint16_t size, int8_t rssi) {
  if (size < sizeof(pnet::DataHdr) || data[0] != pnet::MAGIC || data[1] != pnet::T_DATA) return;
  pnet::DataHdr h;
  memcpy(&h, data, sizeof(h));
  if (size < sizeof(h) + h.n * sizeof(pnet::Rec)) return;
  int pi = find_probe(src, true);
  PInfo &p = T.p[pi];
  uint32_t hub_now = pnet::now_s();
  // the hub has no clock of its own after a power cut: borrow a probe's (they keep it in sleep)
  if (hub_now < TIME_VALID && (h.flags & pnet::DF_ABS_TIME) && h.now >= TIME_VALID) {
    struct timeval tv; tv.tv_sec = h.now; tv.tv_usec = 0; settimeofday(&tv, nullptr);
    hub_now = h.now;
  }
  if (h.now < p.last_probe_now) p.last_raw_t = 0;        // the probe's clock restarted (RST)
  const pnet::Rec *recs = reinterpret_cast<const pnet::Rec *>(data + sizeof(h));
  for (int i = 0; i < h.n; i++) {
    pnet::Rec r;
    memcpy(&r, &recs[i], sizeof(r));
    if (r.t != 0 && r.t <= p.last_raw_t) continue;      // already have it (answer was lost)
    HRec o{};
    if (r.t >= TIME_VALID) o.t = r.t;
    else if (h.now < TIME_VALID && hub_now >= TIME_VALID && r.t <= h.now) o.t = hub_now - (h.now - r.t);
    else o.t = 0;
    o.moist10 = r.moist10; o.vbat_mv = r.vbat_mv;
    o.raw_mv = (i == h.n - 1 && h.unsent == 0) ? h.raw_mv : 0;
    o.probe = (uint8_t) pi; o.rssi = rssi;
    log_append(o);
    if (r.t) p.last_raw_t = r.t;
  }
  p.last_probe_now = h.now;
  p.seen = hub_now >= TIME_VALID ? hub_now : 0;
  p.seen_up = uptime_s();
  p.vbat_mv = h.vbat_mv; p.moist10 = h.moist10; p.raw_mv = h.raw_mv;
  p.li = h.li; p.dry_mv = h.dry_mv; p.wet_mv = h.wet_mv; p.re = h.re; p.st = h.st;
  p.rssi = rssi; p.unsent = h.unsent; p.cfg_ver = h.cfg_ver;
  memcpy(p.fw, h.fw, sizeof(p.fw));
  if (p.p_active && h.cfg_ver == p.p_ver) p.p_active = 0;  // the probe confirmed the new settings
  dirty = true;
  mqtt_dirty[pi] = true;
  rx_count++;
  last_rx_probe = pi;
  led_pulse = true;

  pnet::Ack a{};
  a.magic = pnet::MAGIC; a.type = pnet::T_ACK; a.ver = pnet::VER;
  a.seq = h.seq;
  a.accepted = h.n;
  a.hub_time = hub_now >= TIME_VALID ? hub_now : 0;
  a.channel = channel();
  if (p.p_active) {
    a.flags |= pnet::AF_SETTINGS;
    a.cfg_ver = p.p_ver;
    a.cmd = p.p_cmd;
    a.li = p.p_li; a.dry_mv = p.p_dry; a.wet_mv = p.p_wet; a.re = p.p_re; a.st = p.p_st;
  }
  if (radio) {
    if (!esp_now_is_peer_exist(src)) radio->add_peer(src);
    std::vector<uint8_t> v(reinterpret_cast<uint8_t *>(&a), reinterpret_cast<uint8_t *>(&a) + sizeof(a));
    radio->send(src, v);
  }
}

// ------------------------------------------------------------------ phone (BLE) helpers
inline std::string probe_text(int i) {
  if (i < 0 || i >= MAXP || !T.p[i].used) return "i=-1";
  const PInfo &p = T.p[i];
  char fw[9]; memcpy(fw, p.fw, 8); fw[8] = 0;
  char lb[21]; memcpy(lb, p.label, 20); lb[20] = 0;
  char b[400];
  snprintf(b, sizeof(b),
           "i=%d;name=%s;label=%s;m=%.1f;vb=%.2f;raw=%.3f;seen=%u;ago=%u;rssi=%d;li=%u;re=%u;st=%u;dry=%.3f;wet=%.3f;"
           "fw=%s;uns=%u;cv=%u;pend=%u;pli=%u;pre=%u;pst=%u;pdry=%.3f;pwet=%.3f;pcmd=%u",
           i, probe_name(p.mac).c_str(), lb, p.moist10 / 10.0f, p.vbat_mv / 1000.0f, p.raw_mv / 1000.0f,
           (unsigned) p.seen, (unsigned) (uptime_s() - p.seen_up), p.rssi, p.li, p.re, p.st,
           p.dry_mv / 1000.0f, p.wet_mv / 1000.0f, fw, p.unsent, p.cfg_ver, p.p_active, p.p_li, p.p_re, p.p_st,
           p.p_dry / 1000.0f, p.p_wet / 1000.0f, p.p_cmd);
  return b;
}

// S<US>i<US>li<US>re<US>st<US>dry<US>wet<US>label  (empty = keep; dry/wet "raw" = probe's last reading)
inline void set_pending(const std::vector<std::string> &f) {
  if (f.size() < 8) return;
  int i = atoi(f[1].c_str());
  if (i < 0 || i >= MAXP || !T.p[i].used) return;
  PInfo &p = T.p[i];
  bool any = false;
  auto num = [](const std::string &s) { return (float) atof(s.c_str()); };
  if (!f[2].empty()) { p.p_li = (uint16_t) num(f[2]); any = true; }
  if (!f[3].empty()) { p.p_re = (uint8_t) num(f[3]); any = true; }
  if (!f[4].empty()) { p.p_st = (uint8_t) num(f[4]); any = true; }
  if (!f[5].empty()) { p.p_dry = f[5] == "raw" ? p.raw_mv : (uint16_t) (num(f[5]) * 1000.0f); any = true; }
  if (!f[6].empty()) { p.p_wet = f[6] == "raw" ? p.raw_mv : (uint16_t) (num(f[6]) * 1000.0f); any = true; }
  {                                    // name from the phone; HA gets the new device name
    std::string l;
    for (char c : f[7]) if (c != '"' && c != '\\' && (unsigned char) c >= 0x20) l += c;
    if (l.size() > sizeof(p.label)) {  // cut, but not inside a UTF-8 letter
      size_t n = sizeof(p.label);
      while (n > 0 && ((unsigned char) l[n] & 0xC0) == 0x80) n--;
      l.resize(n);
    }
    if (strncmp(l.c_str(), p.label, sizeof(p.label)) != 0) { mqtt_disc[i] = false; mqtt_dirty[i] = true; }
    memset(p.label, 0, sizeof(p.label));
    memcpy(p.label, l.data(), l.size());
  }
  if (any) {
    if (!p.p_active) { p.p_cmd = 0; }
    p.p_ver = (uint8_t) (p.cfg_ver + 1 == 0 ? 1 : p.cfg_ver + 1);
    if (p.p_ver == p.cfg_ver) p.p_ver++;
    p.p_active = 1;
  }
  dirty = true;
}
inline void set_cmd(int i, uint8_t cmd) {
  if (i < 0 || i >= MAXP || !T.p[i].used) return;
  PInfo &p = T.p[i];
  p.p_cmd |= cmd;
  p.p_ver = (uint8_t) (p.cfg_ver + 1 == 0 ? 1 : p.cfg_ver + 1);
  p.p_active = 1;
  dirty = true;
}
inline void cancel_pending(int i) {
  if (i < 0 || i >= MAXP) return;
  T.p[i].p_active = 0; T.p[i].p_cmd = 0; T.p[i].p_li = T.p[i].p_re = T.p[i].p_st = 0; T.p[i].p_dry = T.p[i].p_wet = 0;
  dirty = true;
}
inline void forget(int i) { if (i >= 0 && i < MAXP) { memset(&T.p[i], 0, sizeof(PInfo)); dirty = true; } }

// ------------------------------------------------------------------ Home Assistant (MQTT)
// state: plant-hub/<probe>/state  {"moisture":..,"battery":..,"rssi":..}; discovery once per connection
inline std::string json_state(int i) {
  const PInfo &p = T.p[i];
  char b[160];
  snprintf(b, sizeof(b), "{\"moisture\":%.1f,\"battery\":%.2f,\"raw\":%.3f,\"rssi\":%d,\"seen\":%u}",
           p.moist10 / 10.0f, p.vbat_mv / 1000.0f, p.raw_mv / 1000.0f, p.rssi, (unsigned) p.seen);
  return b;
}
inline std::string json_discovery(int i, const char *what, const std::string &hub_name) {
  const PInfo &p = T.p[i];
  std::string n = probe_name(p.mac);
  char lb[21]; memcpy(lb, p.label, 20); lb[20] = 0;
  std::string dev_name = lb[0] ? std::string(lb) : n;
  std::string s = "{\"name\":\"";
  bool moist = strcmp(what, "moisture") == 0, batt = strcmp(what, "battery") == 0;
  s += moist ? "Wilgotno\\u015b\\u0107 gleby" : batt ? "Bateria" : "Sygna\\u0142";
  s += "\",\"uniq_id\":\"" + n + "_" + what + "\",\"stat_t\":\"" + hub_name + "/" + n + "/state\",";
  s += "\"val_tpl\":\"{{ value_json." + std::string(what) + " }}\",";
  if (moist) s += "\"unit_of_meas\":\"%\",\"dev_cla\":\"moisture\",\"stat_cla\":\"measurement\",";
  else if (batt) s += "\"unit_of_meas\":\"V\",\"dev_cla\":\"voltage\",\"stat_cla\":\"measurement\",";
  else s += "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\",\"ent_cat\":\"diagnostic\",";
  s += "\"dev\":{\"ids\":[\"" + n + "\"],\"name\":\"" + dev_name + "\",\"mdl\":\"Plant sensor\",\"mf\":\"DIY\"}}";
  return s;
}

}  // namespace hub
