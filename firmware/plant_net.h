// Plant sensor <-> hub radio protocol (ESP-NOW) and the probe side of it.
// One wake: the probe sends its newest unsent readings (max 25 per packet, up to 8 packets),
// the hub answers each packet with an ACK that carries its clock and, when the user changed
// this probe's settings in the hub, the new settings. No router involved; ~0.1-0.3 s radio time.
// Shared by the probe firmware and the hub firmware (hub_core.h).
#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include "esp_attr.h"
#include "esp_wifi.h"
#include "esp_timer.h"

namespace pnet {

static const uint8_t MAGIC = 'P';
static const uint8_t VER = 1;
static const uint8_t T_DATA = 'D';
static const uint8_t T_ACK = 'A';
static const uint32_t TIME_VALID = 1700000000UL;  // seconds: anything below is "clock not set"
static const int MAX_RECS = 25;

enum : uint8_t { DF_ABS_TIME = 1, DF_DISCOVER = 2 };
enum : uint8_t { AF_SETTINGS = 1 };
enum : uint8_t { CMD_CLEAR_LOG = 1 };

struct __attribute__((packed)) Rec {   // same layout as plog::Rec
  uint32_t t;
  uint16_t moist10;
  uint16_t vbat_mv;
};

struct __attribute__((packed)) DataHdr {
  uint8_t magic, type, ver, flags;
  uint16_t seq;
  uint8_t cfg_ver;   // settings version this probe has applied
  uint8_t n;         // records that follow
  uint32_t now;      // probe clock now (record times are on the same clock)
  uint16_t vbat_mv, moist10, raw_mv;   // current reading
  uint16_t li;       // log interval, min
  uint16_t dry_mv, wet_mv;
  uint8_t re, st;    // report every (readings), service mode timeout (min)
  uint16_t unsent;   // readings still waiting after this packet
  char fw[8];
};
static_assert(sizeof(DataHdr) == 36, "DataHdr size");

struct __attribute__((packed)) Ack {
  uint8_t magic, type, ver, flags;
  uint16_t seq;
  uint8_t accepted;  // records taken from this packet
  uint8_t cfg_ver;   // version of the settings below (AF_SETTINGS)
  uint32_t hub_time; // unix seconds, 0 = unknown
  uint8_t channel;   // radio channel the hub listens on
  uint8_t cmd;       // CMD_* one-shot commands
  uint16_t li, dry_mv, wet_mv;  // 0 = keep
  uint8_t re, st;               // 0 = keep
};
static_assert(sizeof(Ack) == 22, "Ack size");

inline uint32_t now_s() { return (uint32_t) ::time(nullptr); }

// ------------------------------------------------------------------ probe side
#ifdef PLANT_LOG_H     // probe firmware only (needs the measurement log)
// Clock offset learned when the hub (or the phone) set the clock during this power-up:
// readings stored earlier with "seconds since start" times can still be converted.
RTC_DATA_ATTR static int64_t rel_base = 0;

inline void set_clock(uint32_t unix_s) {
  uint32_t before = now_s();
  if (before < TIME_VALID && unix_s >= TIME_VALID) rel_base = (int64_t) unix_s - before;
  struct timeval tv;
  tv.tv_sec = unix_s;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

struct Probe {
  esphome::espnow::ESPNowComponent *c = nullptr;
  // inputs
  uint8_t hub[6]{};        // all zero = no hub yet
  uint8_t ch = 1;
  uint8_t cfg_ver = 0;
  uint32_t unsent = 0;
  uint8_t fails = 0;
  bool allow_scan = false;
  DataHdr cur{};           // current reading + settings (seq, n, now, unsent filled per packet)
  // state
  int phase = 0;           // 0 done, 1 waiting for ACK (unicast), 2 scanning
  uint32_t t0 = 0;
  uint8_t scan_ch = 1;
  uint8_t packets = 0;
  uint8_t sent_n = 0;
  uint16_t seq = 0;
  volatile bool got_ack = false;
  Ack ack{};
  uint8_t ack_src[6]{};
  // results
  bool paired_now = false;
  bool settings_rx = false;
  Ack settings{};
  uint32_t delivered = 0;
  int8_t rssi = 0;
  const char *result = "-";
};
static Probe P;
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

inline bool hub_known() { for (int i = 0; i < 6; i++) if (P.hub[i]) return true; return false; }
inline uint32_t ms() { return (uint32_t) (esp_timer_get_time() / 1000); }

inline void set_channel(uint8_t ch) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
}

// build and send one packet with the oldest unsent readings
inline void send_packet(const uint8_t *dst, bool discover) {
  uint8_t buf[sizeof(DataHdr) + MAX_RECS * sizeof(Rec)];
  DataHdr h = P.cur;
  h.magic = MAGIC; h.type = T_DATA; h.ver = VER;
  h.seq = ++P.seq;
  h.cfg_ver = P.cfg_ver;
  h.now = now_s();
  h.flags = (h.now >= TIME_VALID ? DF_ABS_TIME : 0) | (discover ? DF_DISCOVER : 0);
  uint32_t total = plog::size();
  if (P.unsent > total) P.unsent = total;
  uint32_t n = P.unsent < (uint32_t) MAX_RECS ? P.unsent : (uint32_t) MAX_RECS;
  uint32_t first = total - P.unsent;            // oldest unsent, 0 = oldest in the log
  Rec *recs = reinterpret_cast<Rec *>(buf + sizeof(DataHdr));
  for (uint32_t i = 0; i < n; i++) {
    plog::Rec r;
    if (!plog::get(first + i, r)) { n = i; break; }
    uint32_t t = r.t;
    if (t < TIME_VALID && rel_base != 0 && h.now >= TIME_VALID) t = (uint32_t) (t + rel_base);
    recs[i].t = t; recs[i].moist10 = r.moist10; recs[i].vbat_mv = r.vbat_mv;
  }
  h.n = (uint8_t) n;
  h.unsent = (uint16_t) ((P.unsent - n) > 65535 ? 65535 : (P.unsent - n));
  memcpy(buf, &h, sizeof(h));
  P.sent_n = (uint8_t) n;
  P.got_ack = false;
  P.t0 = ms();
  P.packets++;
  std::vector<uint8_t> v(buf, buf + sizeof(DataHdr) + n * sizeof(Rec));
  P.c->send(dst, v);
}

inline void start_scan() {
  P.phase = 2;
  P.scan_ch = 1;
  set_channel(P.scan_ch);
  P.c->add_peer(BCAST);
  send_packet(BCAST, true);
}

// call once after filling P.cur / P.hub / P.ch / P.cfg_ver / P.unsent / P.fails / P.allow_scan
inline void start() {
  P.phase = 0; P.packets = 0; P.paired_now = false; P.settings_rx = false; P.delivered = 0;
  P.result = "no hub";
  if (hub_known()) {
    set_channel(P.ch);
    P.c->add_peer(P.hub);
    P.phase = 1;
    send_packet(P.hub, false);
  } else if (P.allow_scan) {
    start_scan();
  }
}

// received packet (from the espnow on_receive / on_unknown_peer triggers)
inline void on_packet(const uint8_t *src, const uint8_t *data, uint16_t size, int8_t rssi) {
  if (size < sizeof(Ack) || data[0] != MAGIC || data[1] != T_ACK) return;
  Ack a;
  memcpy(&a, data, sizeof(a));
  if (a.seq != P.seq) return;
  P.ack = a;
  memcpy(P.ack_src, src, 6);
  P.rssi = rssi;
  P.got_ack = true;
}

inline void take_ack() {
  const Ack &a = P.ack;
  uint32_t acc = a.accepted > P.sent_n ? P.sent_n : a.accepted;
  P.unsent = P.unsent > acc ? P.unsent - acc : 0;
  P.delivered += acc;
  P.fails = 0;
  if (a.hub_time >= TIME_VALID) {
    uint32_t n = now_s();
    if (n < TIME_VALID || (n > a.hub_time ? n - a.hub_time : a.hub_time - n) > 2) set_clock(a.hub_time);
  }
  if (a.flags & AF_SETTINGS) { P.settings = a; P.settings_rx = true; }
  P.result = "ok";
}

// returns true while busy; call every few ms
inline bool step() {
  uint32_t now = ms();
  if (P.phase == 0) return false;
  if (P.got_ack) {
    if (P.phase == 2) {                            // found the hub while scanning
      memcpy(P.hub, P.ack_src, 6);
      P.ch = P.ack.channel ? P.ack.channel : P.scan_ch;
      P.paired_now = true;
      P.c->add_peer(P.hub);
      if (P.ch != P.scan_ch) set_channel(P.ch);
      P.phase = 1;
    }
    take_ack();
    if (P.unsent > 0 && P.packets < 8 && !(P.ack.cmd & CMD_CLEAR_LOG)) { send_packet(P.hub, false); return true; }
    P.phase = 0;
    return false;
  }
  if (P.phase == 1 && now - P.t0 > 200) {          // no answer from the known hub
    if (P.fails < 255) P.fails++;
    P.result = "no answer";
    if (P.allow_scan && P.fails >= 3) { start_scan(); return true; }
    P.phase = 0;
    return false;
  }
  if (P.phase == 2 && now - P.t0 > 70) {           // next channel
    if (++P.scan_ch > 13) { P.result = "hub not found"; P.phase = 0; return false; }
    set_channel(P.scan_ch);
    send_packet(BCAST, true);
  }
  return true;
}
#endif  // PLANT_LOG_H

}  // namespace pnet
