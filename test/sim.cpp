// Simulated Ninja 300 K-line: MCP2003A transceiver (echo, CS, optional 25 ms cut-off), the
// engine ECU on 0x11 and an ABS ECU on 0x28, speaking KWP2000 with fast init like the KDS.
#include "mock/Arduino.h"
#include <vector>
#include <map>
uint64_t g_us = 1000000;
SerialMock Serial; EspMock ESP; HardwareSerial Serial2(2);
#include "mock/WiFi.h"
#include "mock/ESPmDNS.h"
WiFiMock WiFi; MDNSMock MDNS;
std::map<std::string, std::string> g_nvs;

struct Bike {
  float rpm = 1300, kmh = 0, ect = 85, iat = 30, mapKpa = 40, baroKpa = 101, vbat = 13.8f, subV = 0.8f;
  int tps = 210, gear = 0;
  std::vector<std::pair<uint8_t, uint8_t>> cur, sto;
} bike;

struct SimCfg {
  bool keyOn = true;      // ECU powered
  bool xcvr = true;       // transceiver powered and wired
  float cutMs = 0;        // MCP2003A dominant time-out: 0 = never cuts the wake-up pulse
  uint8_t needTester = 0; // ECU only answers this tester address (0 = any)
  bool abs = true;        // ABS ECU on the same line
  int p2 = 30;            // ms from request to reply
} simc;

int g_frames = 0, g_wakes = 0, g_badWakes = 0, g_starts = 0;
uint8_t g_lastTester = 0;

struct RxB { uint64_t at; uint8_t b; };
static std::deque<RxB> rxq;
static uint64_t txBusyUntil = 0;
static bool invLow = false, mosLow = false, csHigh = false;
static uint64_t lowStart = 0;
static uint64_t wokeAt = 0;
static bool woke = false;
static const int INIT_PIN = 25, CS_PIN = 4;

void sim_advance_us(uint64_t us) { g_us += us; }

static bool xcvrOn() { return simc.xcvr && csHigh; }
static bool busLow() {
  if (mosLow) return true;
  if (!invLow || !xcvrOn()) return false;
  if (simc.cutMs > 0 && g_us - lowStart >= (uint64_t)(simc.cutMs * 1000)) return false;
  return true;
}
int sim_bus_level() { return xcvrOn() || mosLow ? (busLow() ? 0 : 1) : 1; }

static void pulseEnd(uint64_t lowUs) {
  g_wakes++;
  if (lowUs >= 24000 && lowUs <= 26500) { woke = true; wokeAt = g_us; }
  else { g_badWakes++; woke = false; }
}
int sim_uart_inverse(uint32_t mask) {
  if (mask) { invLow = true; lowStart = g_us; }
  else if (invLow) {
    invLow = false;
    if (xcvrOn()) {
      uint64_t d = g_us - lowStart;
      if (simc.cutMs > 0 && d > simc.cutMs * 1000) d = simc.cutMs * 1000;
      pulseEnd(d);
    }
  }
  return 0;
}
void sim_pin_write(int pin, int v) {
  if (pin == CS_PIN) csHigh = v;
  if (pin == INIT_PIN) {
    if (v && !mosLow) { mosLow = true; lowStart = g_us; }
    else if (!v && mosLow) { mosLow = false; pulseEnd(g_us - lowStart); }
  }
}
void sim_kl_begin(unsigned long, int, int) {}

static void rxAt(uint64_t at, uint8_t b) {
  auto it = rxq.end();
  while (it != rxq.begin() && (it - 1)->at > at) --it;
  rxq.insert(it, {at, b});
}

// ---------- ECU nodes ----------
struct Node { uint8_t addr; bool session = false; uint8_t tester = 0; uint64_t until = 0; };
static Node eng{0x11}, absn{0x28};
static Node *node(uint8_t a) { return a == 0x11 ? &eng : (a == 0x28 && simc.abs ? &absn : nullptr); }

static void send(uint8_t src, uint8_t dst, std::vector<uint8_t> d, uint64_t t, bool shortFmt = false) {
  if (!simc.xcvr) return;
  std::vector<uint8_t> f;
  if (shortFmt) f = {(uint8_t)(0x80 | d.size()), dst, src};
  else f = {0x80, dst, src, (uint8_t)d.size()};
  f.insert(f.end(), d.begin(), d.end());
  uint8_t cs = 0;
  for (uint8_t b : f) cs += b;
  f.push_back(cs);
  uint64_t at = t + simc.p2 * 1000;
  for (uint8_t b : f) { rxAt(at, b); at += 1000; }
}

static const std::vector<uint8_t> SUPP = {0x02, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x20,
                                          0x40, 0x44, 0x56, 0x5B, 0x5E, 0x5F, 0x60, 0x61, 0x62};
static bool supp(uint8_t r) { for (uint8_t s : SUPP) if (s == r) return true; return false; }

static std::vector<uint8_t> regData(uint8_t r) {
  auto mm = [](float kpa) { return kpa / 0.133322f; };
  switch (r) {
    case 0x00: case 0x20: case 0x40: case 0x60: {
      uint8_t b[4] = {0};
      for (uint8_t s : SUPP) if (s > r && s <= r + 32) { int i = s - r - 1; b[i / 8] |= 0x80 >> (i % 8); }
      return {b[0], b[1], b[2], b[3]};
    }
    case 0x02: return {0};
    case 0x04: return {(uint8_t)(bike.tps >> 8), (uint8_t)bike.tps};
    case 0x05: { int raw = mm(bike.mapKpa) * 65536 / 950; return {(uint8_t)(raw >> 8), (uint8_t)raw}; }
    case 0x06: return {(uint8_t)lroundf(bike.ect * 1.6f + 48)};
    case 0x07: return {(uint8_t)lroundf(bike.iat * 1.6f + 48)};
    case 0x08: return {(uint8_t)lroundf(mm(bike.baroKpa) * 256 / 950)};
    case 0x09: { int a = (int)bike.rpm / 100, b = lroundf(((int)bike.rpm % 100) * 2.56f); return {(uint8_t)a, (uint8_t)b}; }
    case 0x0A: return {(uint8_t)lroundf(bike.vbat * 256 / 20)};
    case 0x0B: return {(uint8_t)bike.gear};
    case 0x0C: { int s = lroundf(bike.kmh * 2); return {(uint8_t)(s >> 8), (uint8_t)s}; }
    case 0x44: return {0x00, 0x02, 0x31, 0x1E};
    case 0x56: return {(uint8_t)bike.sto.size()};
    case 0x5B: return {(uint8_t)lroundf(bike.subV * 256 / 5)};
    case 0x5E: return {0xCF};
    case 0x5F: return {0xB5};
    case 0x61: return {0x7F};
    case 0x62: return {0x00, 0x00};
  }
  return {};
}

static std::vector<uint8_t> codes(uint8_t sid, const std::vector<std::pair<uint8_t, uint8_t>> &l) {
  std::vector<uint8_t> d = {sid, (uint8_t)l.size()};
  for (auto &c : l) { d.push_back(c.first); d.push_back(c.second); }
  return d;
}

static void handle(const std::vector<uint8_t> &f, uint64_t t) {
  g_frames++;
  if (!simc.keyOn) return;
  uint8_t fmt = f[0];
  int hdr = (fmt & 0xC0) ? 3 : 1, len = fmt & 0x3F;
  if (len == 0) { len = f[3]; hdr = 4; }
  uint8_t tgt = f[1], src = f[2];
  std::vector<uint8_t> d(f.begin() + hdr, f.begin() + hdr + len);
  Node *n = node(tgt);
  if (!n) return;
  if (fmt == 0x81 && d.size() == 1 && d[0] == 0x81) {     // StartCommunication
    if (!woke || g_us - wokeAt > 100000) return;
    woke = false;
    g_lastTester = src;
    if (simc.needTester && src != simc.needTester) return;
    g_starts++;
    eng.session = absn.session = false;
    n->session = true; n->tester = src; n->until = t + 2000000;
    send(tgt, src, {0xC1, 0xEA, 0x8F}, t, true);
    return;
  }
  if (!n->session || n->tester != src || t > n->until) { n->session = false; return; }
  n->until = t + 2000000;
  uint8_t sid = d[0];
  auto rep = [&](std::vector<uint8_t> r) { send(tgt, src, r, t); };
  if (sid == 0x10) { rep({0x50, d.size() > 1 ? d[1] : (uint8_t)0}); return; }
  if (sid == 0x3E) { rep({0x7E}); return; }
  if (sid == 0x82) { rep({0xC2}); n->session = false; return; }
  if (n == &absn) {
    if (sid == 0x1A) { std::vector<uint8_t> r = {0x5A}; for (char c : std::string("ABS-SIM")) r.push_back(c); rep(r); return; }
    if (sid == 0x13 && d.size() > 1) { rep(d[1] == 1 ? std::vector<uint8_t>{0x53, 1, 0x42, 0x00} : std::vector<uint8_t>{0x53, 0}); return; }
    rep({0x7F, sid, 0x11});
    return;
  }
  if (sid == 0x21 && d.size() == 2) {
    uint8_t r = d[1];
    bool bitmap = r == 0 || r == 0x20 || r == 0x40 || r == 0x60;
    if (!bitmap && !supp(r)) { rep({0x7F, 0x21, 0x10}); return; }
    if (bitmap && r == 0x80) { rep({0x7F, 0x21, 0x10}); return; }
    std::vector<uint8_t> v = {0x61, r};
    auto x = regData(r);
    v.insert(v.end(), x.begin(), x.end());
    rep(v);
    return;
  }
  if (sid == 0x1A) { std::vector<uint8_t> r = {0x5A}; for (char c : std::string("21175-0861")) r.push_back(c); rep(r); return; }
  if (sid == 0x13 && d.size() > 1) { rep(codes(0x53, d[1] == 1 ? bike.cur : bike.sto)); return; }
  if (sid == 0x14) { bike.cur.clear(); bike.sto.clear(); rep({0x54}); return; }
  if (sid == 0x12 && d.size() >= 4) {
    int i = d[1];
    if (i >= 1 && i <= (int)bike.sto.size()) rep({0x52, (uint8_t)i, 0x01, bike.sto[i - 1].first, bike.sto[i - 1].second});
    else rep({0x7F, 0x12, 0x12});
    return;
  }
  rep({0x7F, sid, 0x11});
}

// Bytes the tester sends: assemble frames like the ECU does (inter-byte gap > 25 ms = new frame)
static std::vector<uint8_t> ebuf;
static uint64_t lastByte = 0;
static void ecuRx(uint8_t b, uint64_t t) {
  if (t - lastByte > 25000) ebuf.clear();
  lastByte = t;
  ebuf.push_back(b);
  uint8_t fmt = ebuf[0];
  size_t hdr = (fmt & 0xC0) ? 3 : 1, len = fmt & 0x3F;
  if (len == 0) { if (ebuf.size() < 4) return; len = ebuf[3]; hdr = 4; }
  size_t total = hdr + len + 1;
  if (ebuf.size() < total) return;
  uint8_t cs = 0;
  for (size_t i = 0; i + 1 < total; i++) cs += ebuf[i];
  std::vector<uint8_t> f(ebuf.begin(), ebuf.begin() + total);
  ebuf.clear();
  if (cs == f.back()) handle(f, t);
}

void sim_kl_write(uint8_t b) {
  uint64_t start = std::max(g_us, txBusyUntil), end = start + 960;
  txBusyUntil = end;
  if (xcvrOn()) { rxAt(end + 20, b); ecuRx(b, end); }
}
void sim_kl_flush() { if (txBusyUntil > g_us) g_us = txBusyUntil; }
int sim_kl_available() { int n = 0; for (auto &x : rxq) { if (x.at > g_us) break; n++; } return n; }
int sim_kl_read() { if (rxq.empty() || rxq.front().at > g_us) return -1; int b = rxq.front().b; rxq.pop_front(); return b; }
