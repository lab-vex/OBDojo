/*
  OBDojo firmware
  Kawasaki Ninja 300 (EX300, 2013-2017) KDS diagnostic tool
  ESP32-WROOM-32E + MCP2003A K-line transceiver

  The Ninja's 4-pin KDS plug is a single-wire K-line running KWP2000 (ISO 14230) at 10,400 baud
  with fast init. The engine ECU answers on address 0x11 and gives one value per request
  (service 0x21 "read data by local ID", which Kawasaki calls registers).

  Links (all optional, set in Settings):
    Wi-Fi hotspot  -> web page at http://1.1.1.1 (or http://obdojo.local on home Wi-Fi)
    Wi-Fi hotspot  -> OBDojo Android app (same /api as the web page)
    BT Classic     -> ELM327 emulator for Torque Pro or the Teltonika tracker, or the
                      OBDojo Android app (lines starting with '!' get JSON replies). One at a time.
    USB serial     -> same text commands as the app (115200 baud)

  Arduino IDE: Board "ESP32 Dev Module", Partition Scheme "Huge APP (3MB No OTA/1MB SPIFFS)".
  No extra libraries needed. Keep obdojo.ino, obdojo_fw.cpp and web_ui.h together.

  Wiring (full list in docs/HARDWARE.md):
    MCP2003A RXD (1) -> GPIO16 + 4.7k pull-up to 3V3
    MCP2003A CS  (2) -> 1k -> GPIO4 (driven high = transceiver on)
    MCP2003A TXD (4) -> GPIO17
    MCP2003A VSS (5) -> GND, shared with the bike
    MCP2003A LBUS(6) -> K-line (OBD2 pin 7), plus 1k + 1N4148 pull-up from VBB
    MCP2003A VBB (7) -> bike +12 V (OBD2 pin 16), fused
    Optional battery sense: 12V -> 47k -> GPIO34 -> 10k -> GND (vbat_pin 34, vbat_mult 5.7)
    Optional init MOSFET: GPIO25 -> 2N7000 gate, drain on K-line (kl_init 25), only if the
      MCP2003A's 25 ms dominant timeout cuts the wake-up pulse short (Tools > K-line test shows it)

  NOTHING here writes to the ECU. Only read services, clear codes and tester-present are sent
  unless "Allow raw commands" is turned on, and even then the flashing / write services are blocked.
*/

#include <Arduino.h>
#include <stdarg.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "esp_arduino_version.h"
#if __has_include("esp_mac.h")
#include "esp_mac.h"
#endif
#include "driver/uart.h"
#include "driver/gpio.h"
#include "BluetoothSerial.h"
#include "esp_bt.h"
#include "web_ui.h"

#define FW_VERSION "1.0.0"

// On the ESP32 the K-line runs in its own FreeRTOS task, so a slow ECU never stalls Wi-Fi or
// Bluetooth. The PC test build runs the same code inline from loop().
#if defined(ARDUINO_ARCH_ESP32)
#define K_TASK 1
#include "esp_sleep.h"
static portMUX_TYPE dmux = portMUX_INITIALIZER_UNLOCKED;
#define DLOCK() portENTER_CRITICAL(&dmux)
#define DUNLOCK() portEXIT_CRITICAL(&dmux)
#else
#define K_TASK 0
#define DLOCK()
#define DUNLOCK()
#endif

// =====================================================================
//  Settings (stored in NVS, edited from the web page, app or serial)
// =====================================================================
enum SType : uint8_t { S_STR, S_PASS, S_INT, S_BOOL, S_FLOAT, S_SEL };
struct SDef {
  const char *key, *label, *group;
  SType type;
  const char *def;
  bool reboot;
  const char *opts;       // S_SEL: "0:Label|1:Label"
  float mn, mx;           // numeric range, or max length for strings
};

enum {
  K_WIFI_MODE, K_AP_SSID, K_AP_PASS, K_AP_IP, K_AP_CH, K_AP_HIDDEN, K_STA_SSID, K_STA_PASS, K_HOSTNAME,
  K_WEB_USER, K_WEB_PASS, K_CAPTIVE,
  K_BT_NAME, K_TRK_ON, K_TRK_PIN, K_TRK_VIRT,
  K_KL_TX, K_KL_RX, K_KL_CS, K_KL_INIT, K_KL_TESTER, K_KL_ECU, K_KL_P4, K_KL_P3, K_ALLOW_RAW,
  K_REGS, K_RPM_FORM, K_TPS_MIN, K_TPS_MAX,
  K_UNITS, K_LIVE_MS, K_TEMP_WARN, K_RPM_WARN,
  K_SIG1, K_SIG2, K_SIG3, K_SIG4,
  K_VBAT_PIN, K_VBAT_MULT, K_VBAT_LOW,
  K_LOG_MS, K_LOG_AUTO, K_LED_PIN,
  K_SLEEP_MIN, K_SLEEP_S,
  K_COUNT
};

#define SIG_HELP "REG:byte:length:b/l:scale:offset:name:unit"
#define REGS_DEF "rpm=09 tps=04 spd=0C gear=0B ect=06 iat=07 map=05 baro=08 vbat=0A sub=5B"

static const SDef SDEFS[K_COUNT] = {
  {"wifi_mode", "Wi-Fi mode", "Wi-Fi", S_SEL, "0", true, "0:Hotspot only|1:Hotspot + home Wi-Fi|2:Wi-Fi off", 0, 2},
  {"ap_ssid",   "Hotspot name", "Wi-Fi", S_STR, "OBDojo", true, "", 1, 32},
  {"ap_pass",   "Hotspot password (8+ chars, blank = open)", "Wi-Fi", S_PASS, "obdojo1234", true, "", 0, 63},
  {"ap_ip",     "Hotspot address (web page IP)", "Wi-Fi", S_STR, "1.1.1.1", true, "", 7, 15},
  {"ap_ch",     "Hotspot channel", "Wi-Fi", S_INT, "6", true, "", 1, 13},
  {"ap_hidden", "Hide hotspot name", "Wi-Fi", S_BOOL, "0", true, "", 0, 1},
  {"sta_ssid",  "Home Wi-Fi name", "Wi-Fi", S_STR, "", true, "", 0, 32},
  {"sta_pass",  "Home Wi-Fi password", "Wi-Fi", S_PASS, "", true, "", 0, 63},
  {"hostname",  "Network name (.local)", "Wi-Fi", S_STR, "obdojo", true, "", 1, 24},

  {"web_user",  "Web login user (blank = no login)", "Web page", S_STR, "admin", false, "", 0, 24},
  {"web_pass",  "Web login password (blank = no login)", "Web page", S_PASS, "", false, "", 0, 32},
  {"captive",   "Open page automatically on join", "Web page", S_BOOL, "1", true, "", 0, 1},

  {"bt_name",   "Bluetooth name (Torque / tracker see this)", "Bluetooth ELM327", S_STR, "OBDojo", true, "", 1, 24},
  {"trk_on",    "Bluetooth ELM327 link (Torque Pro / tracker)", "Bluetooth ELM327", S_BOOL, "1", true, "", 0, 1},
  {"trk_pin",   "Bluetooth pairing PIN", "Bluetooth ELM327", S_STR, "1234", true, "", 4, 16},
  {"trk_virt",  "Extra Torque PIDs (22F0xx: gear, sub-throttle, custom signals)", "Bluetooth ELM327", S_BOOL, "1", false, "", 0, 1},

  {"kl_tx",     "K-line TX pin (to MCP2003A TXD)", "K-line / ECU", S_INT, "17", true, "", 0, 33},
  {"kl_rx",     "K-line RX pin (to MCP2003A RXD)", "K-line / ECU", S_INT, "16", true, "", 0, 39},
  {"kl_cs",     "Transceiver CS pin (0 = CS wired high)", "K-line / ECU", S_INT, "4", true, "", 0, 33},
  {"kl_init",   "Init MOSFET pin (0 = pulse through TXD)", "K-line / ECU", S_INT, "0", true, "", 0, 33},
  {"kl_tester", "Tester address", "K-line / ECU", S_SEL, "0", false, "0:Auto (F1, then F2)|1:F1|2:F2", 0, 2},
  {"kl_ecu",    "Engine ECU address (hex)", "K-line / ECU", S_STR, "11", false, "", 2, 2},
  {"kl_p4",     "Gap between request bytes (ms)", "K-line / ECU", S_INT, "5", false, "", 0, 20},
  {"kl_p3",     "Gap after each reply (ms)", "K-line / ECU", S_INT, "55", false, "", 25, 500},
  {"allow_raw", "Allow raw ECU commands (advanced)", "K-line / ECU", S_BOOL, "0", false, "", 0, 1},

  // Which KDS register feeds each live value. Defaults are the Z750/Z1000 map from KDS2Bluetooth;
  // use Tools > Register sweep / monitor to confirm them on the EX300. "--" turns one off.
  {"regs",      "Live value registers (name=hex)", "Live data", S_STR, REGS_DEF, false, "", 0, 120},
  {"rpm_form",  "RPM formula", "Live data", S_SEL, "0", false, "0:A×100 + B×100/256|1:A×100 + B", 0, 1},
  {"tps_min",   "Throttle closed (raw)", "Live data", S_INT, "201", false, "", 0, 1023},
  {"tps_max",   "Throttle fully open (raw)", "Live data", S_INT, "895", false, "", 1, 1023},

  {"units",     "Units", "Display", S_SEL, "0", false, "0:Metric|1:Imperial", 0, 1},
  {"live_ms",   "Screen refresh (ms)", "Display", S_INT, "250", false, "", 100, 5000},
  {"temp_warn", "Coolant warning (C)", "Display", S_INT, "105", false, "", 60, 130},
  {"rpm_warn",  "Shift / RPM warning", "Display", S_INT, "12500", false, "", 1000, 15000},

  {"sig1",      "Signal 1 (" SIG_HELP ")", "Custom signals", S_STR, "", false, "", 0, 63},
  {"sig2",      "Signal 2", "Custom signals", S_STR, "", false, "", 0, 63},
  {"sig3",      "Signal 3", "Custom signals", S_STR, "", false, "", 0, 63},
  {"sig4",      "Signal 4", "Custom signals", S_STR, "", false, "", 0, 63},

  {"vbat_pin",  "Battery sense pin (0 = off, use 32-39)", "Battery", S_INT, "0", true, "", 0, 39},
  {"vbat_mult", "Battery divider ratio", "Battery", S_FLOAT, "5.7", false, "", 1, 20},
  {"vbat_low",  "Low battery warning (V)", "Battery", S_FLOAT, "11.8", false, "", 9, 14},

  {"log_ms",    "Log interval (ms)", "Logging", S_INT, "500", false, "", 200, 60000},
  {"log_auto",  "Start logging on power-up", "Logging", S_BOOL, "0", false, "", 0, 1},
  {"led_pin",   "Status LED pin (0 = off)", "System", S_INT, "2", true, "", 0, 33},
  {"sleep_min", "Deep sleep after N min with no ECU and nobody connected (0 = never)", "Power", S_INT, "0", false, "", 0, 240},
  {"sleep_s",   "While asleep, check for the ECU every N seconds", "Power", S_INT, "60", false, "", 10, 3600},
};

static String sv[K_COUNT];
static Preferences prefs;

static int    sInt(int k)   { return sv[k].toInt(); }
static float  sFlt(int k)   { return sv[k].toFloat(); }
static bool   sBool(int k)  { return sv[k].toInt() != 0; }
static const String &sStr(int k) { return sv[k]; }

static void loadSettings() {
  prefs.begin("obdojo", true);
  for (int i = 0; i < K_COUNT; i++) sv[i] = prefs.getString(SDEFS[i].key, SDEFS[i].def);
  prefs.end();
}

static void saveSetting(int k) {
  prefs.begin("obdojo", false);
  prefs.putString(SDEFS[k].key, sv[k]);
  prefs.end();
}

// =====================================================================
//  JSON / hex helpers
// =====================================================================
static String jEsc(const String &s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += c;
    }
  }
  return o;
}
static String q(const String &s) { return "\"" + jEsc(s) + "\""; }
static String errJson(const char *t, const String &msg) {
  return String("{\"t\":\"") + t + "\",\"ok\":false,\"msg\":" + q(msg) + "}";
}
static String toHex(const uint8_t *d, int n, bool sp = true) {
  String o;
  char b[4];
  for (int i = 0; i < n; i++) {
    snprintf(b, sizeof(b), sp && i ? " %02X" : "%02X", d[i]);
    o += b;
  }
  return o;
}
static String toAscii(const uint8_t *d, int n) {
  String o;
  for (int i = 0; i < n; i++) o += (d[i] >= 0x20 && d[i] < 0x7F) ? (char)d[i] : '.';
  return o;
}
static String hex2(uint8_t b) { char s[4]; snprintf(s, sizeof(s), "%02X", b); return s; }
static String fmtF(float v, int dp) { return String(v, (unsigned int)dp); }

static int parseHex(const String &s, uint8_t *out, int maxLen) {
  String h;
  for (size_t i = 0; i < s.length(); i++) if (isxdigit(s[i])) h += s[i];
  int n = 0;
  for (size_t i = 0; i + 1 < h.length() && n < maxLen; i += 2)
    out[n++] = strtoul(h.substring(i, i + 2).c_str(), nullptr, 16);
  return n;
}

// =====================================================================
//  Kawasaki tables
// =====================================================================
// Register names from the Z750 / Z1000 captures in KDS2Bluetooth's "OBD II PIDs.xlsx".
// None of them is confirmed on the EX300 yet; the register sweep shows which ones answer.
struct RegName { uint8_t r; const char *n; };
static const RegName REGNAMES[] = {
  {0x00, "Supported registers 01-20"}, {0x02, "Starter switch"}, {0x04, "Main throttle sensor"},
  {0x05, "Intake air pressure"}, {0x06, "Coolant temp"}, {0x07, "Intake air temp"},
  {0x08, "Atmospheric pressure"}, {0x09, "Engine RPM"}, {0x0A, "Battery voltage"}, {0x0B, "Gear position"},
  {0x0C, "Vehicle speed"}, {0x0E, "Injector 1 time"}, {0x0F, "Injector 2 time"}, {0x10, "Injector 3 time"},
  {0x11, "Injector 4 time"}, {0x16, "Ignition timing cyl 1"}, {0x17, "Ignition timing cyl 2"},
  {0x18, "Ignition timing cyl 3"}, {0x19, "Ignition timing cyl 4"}, {0x1E, "CO adjust 1"},
  {0x1F, "CO adjust 2"}, {0x20, "Supported registers 21-40"}, {0x21, "CO adjust 3"}, {0x22, "CO adjust 4"},
  {0x40, "Supported registers 41-60"}, {0x44, "Engine run time (s)"}, {0x56, "Stored failure count"},
  {0x5B, "Sub-throttle sensor"}, {0x5E, "Vehicle-down sensor"}, {0x5F, "Internal voltage 2"},
  {0x60, "Supported registers 61-80"}, {0x61, "Internal voltage 3"}, {0x62, "Engine stop switch"},
  {0x66, "Exhaust valve angle?"}, {0x6A, "Fuel cut"}, {0x6B, "Engine starting mode"},
  {0x80, "Supported registers 81-A0"}, {0xA0, "Supported registers A1-C0"},
};
static const char *regName(uint8_t r) {
  for (auto &e : REGNAMES) if (e.r == r) return e.n;
  return "";
}

// Kawasaki DFI service codes (the two digits dealer mode shows on the dash)
struct SvcCode { uint8_t c; const char *d; };
static const SvcCode SVC[] = {
  {0x11, "Main throttle sensor"}, {0x12, "Intake air pressure sensor"}, {0x13, "Intake air temp sensor"},
  {0x14, "Water temp sensor"}, {0x15, "Atmospheric pressure sensor"}, {0x16, "Intake air pressure sensor 2"},
  {0x21, "Crankshaft sensor"}, {0x23, "Camshaft position sensor"}, {0x24, "Speed sensor"},
  {0x25, "Speed sensor / gear position"}, {0x31, "Vehicle-down sensor"}, {0x32, "Sub-throttle sensor"},
  {0x33, "Oxygen sensor inactive (if fitted)"}, {0x34, "Exhaust valve sensor"},
  {0x35, "Immobiliser amplifier (if fitted)"}, {0x36, "Blank key (if fitted)"}, {0x39, "ECU communication error"},
  {0x51, "Stick coil 1"}, {0x52, "Stick coil 2"}, {0x53, "Stick coil 3"}, {0x54, "Stick coil 4"},
  {0x56, "Radiator fan relay"}, {0x62, "Sub-throttle valve actuator"}, {0x63, "Exhaust valve actuator"},
  {0x64, "Air switching valve"}, {0x67, "Oxygen sensor heater (if fitted)"}, {0x83, "Fuel pump relay"},
  {0x94, "Oxygen sensor (if fitted)"},
};
static const char *svcDesc(uint8_t c) {
  for (auto &e : SVC) if (e.c == c) return e.d;
  return "";
}

static const char *nrcText(uint8_t c) {
  switch (c) {
    case 0x10: return "general reject";
    case 0x11: return "service not supported";
    case 0x12: return "not supported / bad format";
    case 0x13: return "bad message length";
    case 0x21: return "busy, repeat request";
    case 0x22: return "conditions not correct";
    case 0x31: return "request out of range";
    case 0x33: return "security access denied";
    case 0x35: return "invalid key";
    case 0x36: return "too many attempts";
    case 0x37: return "time delay not expired";
    case 0x78: return "response pending";
    case 0x80: return "not supported in this session";
    default:   return "unknown";
  }
}

// =====================================================================
//  Live data slots
// =====================================================================
enum { L_RPM, L_TPS, L_TPSV, L_SUB, L_MAP, L_BARO, L_SPD, L_ECT, L_IAT, L_VBAT, L_GEAR,
       L_C1, L_C2, L_C3, L_C4, L_N };
static const char *LKEY[L_N]   = {"rpm", "tps", "tpsv", "sub", "map", "baro", "spd", "ect", "iat", "vbat", "gear",
                                  "c1", "c2", "c3", "c4"};
static const float LSCALE[L_N] = {1, 10, 1000, 1000, 10, 10, 10, 10, 10, 100, 1, 100, 100, 100, 100};
static const uint8_t LDP[L_N]  = {0, 1, 3, 3, 1, 1, 1, 1, 1, 2, 0, 2, 2, 2, 2};

// Register-mapped slots (tpsv comes with tps). Order = "regs" setting keys.
enum { R_RPM, R_TPS, R_SPD, R_GEAR, R_ECT, R_IAT, R_MAP, R_BARO, R_VBAT, R_SUB, R_N };
static const char *RKEY[R_N] = {"rpm", "tps", "spd", "gear", "ect", "iat", "map", "baro", "vbat", "sub"};
static const uint8_t RSLOT[R_N] = {L_RPM, L_TPS, L_SPD, L_GEAR, L_ECT, L_IAT, L_MAP, L_BARO, L_VBAT, L_SUB};

struct LiveData { uint32_t ms; float v[L_N]; uint32_t ok; int fi; int dtc; };
static LiveData live = {0, {0}, 0, -1, -1};
static float pkRpm = 0, pkSpd = 0, pkEct = -100;
static uint16_t tpsRaw = 0;          // last main throttle raw reading, for "tps learn"

// ---------- custom signals: decode a value out of any KDS register ----------
struct Sig {
  bool on = false, little = false, sgn = false;
  uint8_t reg = 0, byte = 0, len = 1;
  float scale = 1, off = 0;
  String name, unit;
  float val = 0; uint32_t at = 0;
};
static Sig sigs[4];

static bool sigExtract(const Sig &s, const uint8_t *d, int n, float &out) {
  if (s.byte + s.len > n) return false;
  uint32_t v = 0;
  for (int i = 0; i < s.len; i++) {
    uint8_t b = d[s.byte + i];
    if (s.little) v |= (uint32_t)b << (8 * i);
    else v = (v << 8) | b;
  }
  int32_t sv32 = (int32_t)v;
  if (s.sgn && s.len < 4 && (v & (1UL << (s.len * 8 - 1)))) sv32 = (int32_t)(v | (0xFFFFFFFFUL << (s.len * 8)));
  out = (s.sgn ? (float)sv32 : (float)v) * s.scale + s.off;
  return true;
}

// "REG:byte:length:b/l:scale:offset:name:unit"
//   REG = KDS register in hex (e.g. 5E), byte = position in the register's data (0 = first byte
//   after "61 xx"). b/l = big / little endian, B/L = signed. Returns "" if OK, else the problem.
static String sigParse(const String &txt, Sig &s) {
  s = Sig();
  String t = txt; t.trim();
  if (!t.length()) return "";
  String f[8];
  int nf = 0, start = 0;
  for (int i = 0; i <= (int)t.length() && nf < 8; i++) {
    if (i == (int)t.length() || t[i] == ':') { f[nf++] = t.substring(start, i); start = i + 1; }
  }
  if (nf < 6) return "Format: " SIG_HELP ", e.g. 5E:0:1:b:0.01953:0:Tilt sensor:V";
  for (int i = 0; i < nf; i++) f[i].trim();
  String src = f[0]; src.toUpperCase();
  if (src.startsWith("0X")) src = src.substring(2);
  if (src.startsWith("21") && src.length() == 4) src = src.substring(2);   // "2104" also accepted
  if (src.length() < 1 || src.length() > 2) return "Register must be 2 hex digits, e.g. 5E";
  for (size_t i = 0; i < src.length(); i++) if (!isxdigit(src[i])) return "Register must be hex, e.g. 5E";
  s.reg = strtoul(src.c_str(), nullptr, 16);
  s.byte = f[1].toInt();
  s.len = f[2].toInt();
  if (s.len < 1 || s.len > 4) return "Length must be 1-4 bytes";
  if (s.byte > 30) return "Byte position is past the end of the register";
  char e = f[3].length() ? f[3][0] : 'b';
  if (!strchr("bBlL", e)) return "Endian must be b, l, B or L (capital = signed)";
  s.little = (e == 'l' || e == 'L');
  s.sgn = (e == 'B' || e == 'L');
  s.scale = f[4].toFloat();
  if (s.scale == 0) return "Scale can't be 0";
  s.off = f[5].toFloat();
  s.name = nf > 6 && f[6].length() ? f[6] : String("Signal");
  s.unit = nf > 7 ? f[7] : String("");
  s.on = true;
  return "";
}
static bool sigFresh(const Sig &s) { return s.on && s.at && millis() - s.at < 4000; }

// ---------- "regs" setting: name=hex pairs ----------
static String regsParse(const String &txt, uint8_t *out) {
  // out[R_N]: 0 = off
  uint8_t tmp[R_N];
  {
    String d = REGS_DEF;
    for (int i = 0; i < R_N; i++) tmp[i] = 0;
    // defaults first so a short setting only overrides what it names
    int st = 0;
    for (int i = 0; i <= (int)d.length(); i++) {
      if (i == (int)d.length() || d[i] == ' ') {
        String p = d.substring(st, i); st = i + 1;
        int eq = p.indexOf('=');
        if (eq < 0) continue;
        String k = p.substring(0, eq);
        for (int r = 0; r < R_N; r++) if (k == RKEY[r]) tmp[r] = strtoul(p.substring(eq + 1).c_str(), nullptr, 16);
      }
    }
  }
  String t = txt; t.trim(); t.toLowerCase();
  int st = 0;
  for (int i = 0; i <= (int)t.length(); i++) {
    if (i == (int)t.length() || t[i] == ' ' || t[i] == ',') {
      String p = t.substring(st, i); st = i + 1;
      p.trim();
      if (!p.length()) continue;
      int eq = p.indexOf('=');
      if (eq < 1) return "Use name=hex pairs, e.g. iat=05 map=07";
      String k = p.substring(0, eq), v = p.substring(eq + 1);
      int r = -1;
      for (int j = 0; j < R_N; j++) if (k == RKEY[j]) r = j;
      if (r < 0) return "Unknown name " + k + ". Use rpm tps spd gear ect iat map baro vbat sub";
      if (v == "--" || v == "off" || v == "-") { tmp[r] = 0; continue; }
      if (v.length() < 1 || v.length() > 2) return k + ": register must be 2 hex digits or --";
      for (size_t j = 0; j < v.length(); j++) if (!isxdigit(v[j])) return k + ": register must be hex";
      tmp[r] = strtoul(v.c_str(), nullptr, 16);
    }
  }
  memcpy(out, tmp, R_N);
  return "";
}

// =====================================================================
//  K-line settings copy for the K-line task (plain values, no Strings shared across tasks)
// =====================================================================
struct KCfg {
  int tx = 17, rx = 16, cs = 4, initPin = 0;
  int testerSel = 0;            // 0 auto, 1 F1, 2 F2
  uint8_t ecu = 0x11;
  int p4 = 5, p3 = 55;
  int rpmForm = 0, tpsMin = 201, tpsMax = 895;
  uint8_t reg[R_N];
  int vbatPin = 0; float vbatMult = 5.7f;
};
static KCfg kc;

static void applyKCfg() {
  KCfg n;
  n.tx = sInt(K_KL_TX); n.rx = sInt(K_KL_RX); n.cs = sInt(K_KL_CS); n.initPin = sInt(K_KL_INIT);
  n.testerSel = sInt(K_KL_TESTER);
  n.ecu = strtoul(sStr(K_KL_ECU).c_str(), nullptr, 16);
  if (!n.ecu) n.ecu = 0x11;
  n.p4 = sInt(K_KL_P4); n.p3 = sInt(K_KL_P3);
  n.rpmForm = sInt(K_RPM_FORM); n.tpsMin = sInt(K_TPS_MIN); n.tpsMax = sInt(K_TPS_MAX);
  if (n.tpsMax <= n.tpsMin) n.tpsMax = n.tpsMin + 1;
  if (regsParse(sStr(K_REGS), n.reg).length()) regsParse("", n.reg);
  n.vbatPin = sInt(K_VBAT_PIN); n.vbatMult = sFlt(K_VBAT_MULT);
  DLOCK();
  kc = n;
  DUNLOCK();
}

// =====================================================================
//  Globals
// =====================================================================
BluetoothSerial SerialBT;
WebServer web(80);
DNSServer dns;
HardwareSerial &KL = Serial2;

static bool serialLive = false, logging = false;
static bool webStarted = false, dnsStarted = false, trkStarted = false;
static uint32_t webLiveUntil = 0, rebootAt = 0;
static volatile uint32_t liveWantUntil = 0;    // set by loop, read by the K-line task
static volatile bool ecuFound = false;
static uint8_t suppBits[32];                   // supported registers 01..FF from 21 00/20/40...
static volatile bool suppKnown = false;
static char ecuId[32] = "";

// =====================================================================
//  K-line link layer: KWP2000 (ISO 14230-2) with fast init
// =====================================================================
// Frame: [fmt 0x80][target][source][length][service + data...][checksum = sum & 0xFF]
// StartCommunication alone uses [0x81][target][source][0x81][checksum].
// The transceiver echoes every byte we send back onto RX, so each send reads its own echo first:
// no echo at all means the transceiver isn't powered or wired, which is the most useful
// thing to know on the bike.
#define KL_BAUD 10400

struct KStats {
  uint32_t req, ok, tmo, nrc, cks, echoBad, inits, initOk;
  float initLowMs;      // how long the bus really stayed low in the last wake-up pulse (needs 25)
  int8_t echo;          // -1 unknown, 0 missing / wrong, 1 ok
  uint8_t kb1, kb2, tester, target;
  bool session;
  uint32_t sessionAt, lastOkAt;
  char err[96];
  uint32_t rateN, rateT;
  float rate;           // requests answered per second
};
static KStats ks;
static uint32_t kLastTraffic = 0, kLastRx = 0, kLastReq = 0;
static int kFail = 0;

static void kErr(const char *fmt, ...) {
  char b[96];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof(b), fmt, a);
  va_end(a);
  DLOCK();
  memcpy(ks.err, b, sizeof(b));
  DUNLOCK();
}

// ---------- frame trace (last TR_N frames, for Tools > K-line) ----------
#define TR_N 40
struct TraceE { uint32_t ms; char dir; uint8_t n; uint8_t d[24]; };
static TraceE trace[TR_N];
static int trHead = 0, trCount = 0;
static void trAdd(char dir, const uint8_t *d, int n) {
  if (n > 24) n = 24;
  uint32_t now = millis();
  DLOCK();
  TraceE &e = trace[trHead];
  e.ms = now; e.dir = dir; e.n = n;
  memcpy(e.d, d, n);
  trHead = (trHead + 1) % TR_N;
  if (trCount < TR_N) trCount++;
  DUNLOCK();
}
static void trNote(const char *txt) { trAdd('I', (const uint8_t *)txt, strlen(txt)); }

// ---------- UART ----------
static void klUartBegin() {
  KL.setRxBufferSize(512);
  KL.begin(KL_BAUD, SERIAL_8N1, kc.rx, kc.tx);
}
static void klPinsInit() {
  if (kc.cs > 0) { pinMode(kc.cs, OUTPUT); digitalWrite(kc.cs, HIGH); }   // transceiver on
  if (kc.initPin > 0) { pinMode(kc.initPin, OUTPUT); digitalWrite(kc.initPin, LOW); }
  klUartBegin();
}
static void klFlushRx() { while (KL.available()) KL.read(); }
static int klReadByte(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  for (;;) {
    if (KL.available()) return KL.read();
    if (millis() - t0 >= timeoutMs) return -1;
    delay(1);
  }
}

// Sends a frame byte by byte (gap kl_p4 ms) and checks the echo.
static bool klSendRaw(const uint8_t *f, int n) {
  klFlushRx();
  for (int i = 0; i < n; i++) {
    KL.write(f[i]);
    if (kc.p4 > 0 && i + 1 < n) { KL.flush(); delay(kc.p4); }
  }
  KL.flush();
  trAdd('T', f, n);
  bool ok = true;
  for (int i = 0; i < n; i++) {
    int b = klReadByte(20);
    if (b != f[i]) { ok = false; break; }
  }
  if (!ok) { delay(5); klFlushRx(); }
  kLastTraffic = millis();
  ks.echo = ok ? 1 : 0;
  if (!ok) ks.echoBad++;
  return ok;
}

// Reads one frame. Returns the data length (service byte onwards) and the frame's target /
// source, or -1 = nothing came, -2 = frame cut short, -3 = bad checksum.
static int klRecvFrame(uint8_t *data, int maxLen, uint32_t firstTo, uint8_t &tgt, uint8_t &src) {
  uint8_t raw[300];
  int rn = 0;
  tgt = src = 0;
  int b = klReadByte(firstTo);
  if (b < 0) return -1;
  raw[rn++] = b;
  auto nxt = [&]() -> int {
    int x = klReadByte(40);       // ISO P1 max is 20 ms between ECU bytes
    if (x >= 0 && rn < (int)sizeof(raw)) raw[rn++] = x;
    return x;
  };
  uint8_t fmt = b;
  int len = fmt & 0x3F;
  if (fmt & 0xC0) {
    int t = nxt(), s = nxt();
    if (t < 0 || s < 0) { trAdd('R', raw, rn); return -2; }
    tgt = t; src = s;
  }
  if (len == 0) {
    int l = nxt();
    if (l < 0) { trAdd('R', raw, rn); return -2; }
    len = l;
  }
  for (int i = 0; i <= len; i++) {          // data + checksum
    if (nxt() < 0) { trAdd('R', raw, rn); return -2; }
  }
  kLastRx = kLastTraffic = millis();
  trAdd('R', raw, rn);
  uint8_t sum = 0;
  for (int i = 0; i < rn - 1; i++) sum += raw[i];
  if (sum != raw[rn - 1]) return -3;
  int n = len < maxLen ? len : maxLen;
  memcpy(data, raw + (rn - 1 - len), n);
  return n;
}

static void klFailed() {
  if (++kFail >= 3 && ks.session) {
    ks.session = false;
    ecuFound = false;
    trNote("session lost");
  }
}

// One request on the open session. Returns the reply length (service byte onwards), 0 = none.
static int klXfer(const uint8_t *req, int len, uint8_t *resp, int maxLen, uint32_t to) {
  if (len < 1 || len > 63) return 0;
  uint32_t since = millis() - kLastRx;
  if (since < (uint32_t)kc.p3) delay(kc.p3 - since);
  uint8_t f[72];
  int fn = 0;
  f[fn++] = 0x80; f[fn++] = ks.target; f[fn++] = ks.tester; f[fn++] = len;
  memcpy(f + fn, req, len);
  fn += len;
  uint8_t cs = 0;
  for (int i = 0; i < fn; i++) cs += f[i];
  f[fn++] = cs;
  ks.req++;
  kLastReq = millis();
  if (!klSendRaw(f, fn)) { kErr("No echo from the transceiver. Check RXD, its pull-up, CS and VBB"); klFailed(); return 0; }
  uint32_t dl = millis() + to;
  uint8_t r[260];
  for (;;) {
    int32_t rem = (int32_t)(dl - millis());
    uint8_t t, s;
    int n = klRecvFrame(r, sizeof(r), rem > 0 ? rem : 1, t, s);
    if (n == -1) { ks.tmo++; kErr("No reply to %02X %02X", req[0], len > 1 ? req[1] : 0); klFailed(); return 0; }
    if (n == -2) { ks.tmo++; kErr("Reply cut short"); klFailed(); return 0; }
    if (n == -3) { ks.cks++; kErr("Reply had a bad checksum (noise or wrong baud)"); klFailed(); return 0; }
    if ((t && t != ks.tester) || (s && s != ks.target)) continue;          // not for us
    if (n >= 3 && r[0] == 0x7F && r[2] == 0x78) { dl = millis() + 5000; continue; }  // pending
    if (r[0] == 0x7F) ks.nrc++;
    ks.ok++;
    ks.lastOkAt = millis();
    ks.rateN++;
    if (millis() - ks.rateT >= 2000) {
      ks.rate = ks.rateN * 1000.0f / (millis() - ks.rateT);
      ks.rateN = 0;
      ks.rateT = millis();
    }
    kFail = 0;
    int m = n < maxLen ? n : maxLen;
    memcpy(resp, r, m);
    return m;
  }
}

// Holds the bus low for the 25 ms wake-up, then high for 25 ms (ISO 14230 fast init).
// Watches RX the whole time, so we know how long the bus really stayed low: the MCP2003A
// switches its driver off after "about 25 ms" of dominant, which can cut the pulse short.
static float klWakePulse() {
  bool mos = kc.initPin > 0;
  uint32_t t0 = micros();
  if (mos) digitalWrite(kc.initPin, HIGH);
  else uart_set_line_inverse(UART_NUM_2, UART_SIGNAL_TXD_INV);       // idle-high TX now sits low
  uint32_t lowAt = 0, backHigh = 0;
  bool sawLow = false;
  while ((uint32_t)(micros() - t0) < 25000) {
    int lv = gpio_get_level((gpio_num_t)kc.rx);
    uint32_t t = micros() - t0;
    if (!lv) { if (!sawLow) { sawLow = true; lowAt = t; } backHigh = 0; }
    else if (sawLow && !backHigh) backHigh = t;
  }
  if (mos) digitalWrite(kc.initPin, LOW);
  else uart_set_line_inverse(UART_NUM_2, UART_SIGNAL_INV_DISABLE);
  uint32_t t1 = micros();
  float lowMs = !sawLow ? 0 : ((backHigh ? backHigh : 25000) - lowAt) / 1000.0f;
  while ((uint32_t)(micros() - t1) < 25000) {}
  klFlushRx();          // our own pulse arrives on RX as a break
  return lowMs;
}

static uint8_t kAutoTester = 0xF1;   // auto mode: switches F1 <-> F2 after each attempt the ECU ignores
static uint8_t pickTester() {
  if (kc.testerSel == 1) return 0xF1;
  if (kc.testerSel == 2) return 0xF2;
  return kAutoTester;
}

// Wake the ECU at address tgt and open a diagnostic session
static bool klOpen(uint8_t tgt) {
  uint8_t tester = pickTester();
  ks.inits++;
  ks.session = false;
  uint32_t idle = millis() - kLastTraffic;
  if (idle < 300) delay(300 - idle);          // bus must be idle 300 ms before a wake-up
  float lowMs = klWakePulse();
  ks.initLowMs = lowMs;
  char note[24];
  snprintf(note, sizeof(note), "wake %.1fms %02X>%02X", lowMs, tester, tgt);
  trNote(note);

  uint8_t sc[5] = {0x81, tgt, tester, 0x81, 0};
  for (int i = 0; i < 4; i++) sc[4] += sc[i];
  if (!klSendRaw(sc, 5)) {
    if (lowMs < 1) kErr("No echo and the bus never went low. Check MCP2003A VBB (12 V), CS, TXD and RXD wiring");
    else kErr("No echo from the transceiver. Check RXD and its 4.7k pull-up to 3V3");
    return false;
  }
  uint8_t r[24], t, s;
  int n = klRecvFrame(r, sizeof(r), 150, t, s);
  if (n < 0) {
    if (lowMs > 0.5f && lowMs < 23.5f)
      kErr("No answer. The wake-up pulse was cut to %.1f ms (needs 25): fit the init MOSFET", lowMs);
    else
      kErr("ECU didn't answer the wake-up (tester %02X). Key on? KDS pin 1 to OBD pin 7?", tester);
    if (kc.testerSel == 0) kAutoTester = tester == 0xF1 ? 0xF2 : 0xF1;   // the other address next time
    return false;
  }
  if (r[0] != 0xC1) {
    kErr("Start communication refused: %s", toHex(r, n).c_str());
    return false;
  }
  ks.kb1 = n > 1 ? r[1] : 0;
  ks.kb2 = n > 2 ? r[2] : 0;
  ks.tester = tester;
  ks.target = tgt;
  ks.session = true;
  ks.sessionAt = millis();
  kFail = 0;

  uint8_t ds[2] = {0x10, 0x80};             // Kawasaki diagnostic session
  n = klXfer(ds, 2, r, sizeof(r), 250);
  if (n < 1) { kErr("ECU woke up but ignored the diagnostic session request"); ks.session = false; return false; }
  if (r[0] != 0x50) trNote("diag session refused, carrying on");
  ks.initOk++;
  kErr("%s", "");
  return true;
}

static void klClose() {
  if (!ks.session) return;
  uint8_t rq[1] = {0x82}, r[8];
  klXfer(rq, 1, r, sizeof(r), 150);
  ks.session = false;
}

static void klKeepAlive() {
  if (!ks.session || millis() - kLastReq < 1500) return;
  uint8_t rq[1] = {0x3E}, r[8];
  klXfer(rq, 1, r, sizeof(r), 150);
}

// Request to any ECU address: opens (or switches) the session as needed
static volatile bool kForceReopen = false;    // Tools > K-line test: start a fresh session

static int klRequest(uint8_t tgt, const uint8_t *req, int len, uint8_t *resp, int maxLen, uint32_t to) {
  if (kForceReopen) { kForceReopen = false; klClose(); ks.session = false; ecuFound = false; }
  if (ks.session && ks.target != tgt) klClose();
  if (!ks.session && !klOpen(tgt)) return 0;
  return klXfer(req, len, resp, maxLen, to);
}

// =====================================================================
//  Jobs: requests from the web page / app / Bluetooth, run by the K-line task
// =====================================================================
struct KJob { uint8_t tgt; uint8_t req[64]; int len; uint8_t resp[260]; int n; uint32_t to; };
static KJob job;
static volatile int jobState = 0;      // 0 free, 1 queued, 2 done
#if K_TASK
static SemaphoreHandle_t jobDone;
#endif
static uint8_t ecuAddr() { return kc.ecu; }

static int kJob(uint8_t tgt, const uint8_t *req, int len, uint8_t *resp, int maxLen, uint32_t to) {
  if (len < 1 || len > 63) return 0;
#if K_TASK
  uint32_t t0 = millis();
  while (jobState == 1 && millis() - t0 < 8000) delay(5);     // an earlier job still running
  if (jobState == 1) return 0;
  job.tgt = tgt;
  memcpy(job.req, req, len);
  job.len = len;
  job.to = to;
  job.n = 0;
  xSemaphoreTake(jobDone, 0);
  jobState = 1;
  if (xSemaphoreTake(jobDone, pdMS_TO_TICKS(to + 4000)) != pdTRUE) return 0;
  int n = job.n < maxLen ? job.n : maxLen;
  memcpy(resp, job.resp, n);
  jobState = 0;
  return n;
#else
  return klRequest(tgt, req, len, resp, maxLen, to);
#endif
}
static int ecuReq(const uint8_t *req, int len, uint8_t *resp, int maxLen, uint32_t to) {
  return kJob(ecuAddr(), req, len, resp, maxLen, to);
}

// =====================================================================
//  Background work in the K-line task: connect, live data, sweep, monitor
// =====================================================================
enum { KM_LIVE, KM_SWEEP, KM_MON };
static volatile int kMode = KM_LIVE;
static uint32_t kNextOpen = 0;
static int kOpenFails = 0;
static uint32_t codesAt = 0;
static uint32_t connectedAt = 0;

static bool isSupp(uint8_t reg) {
  if (!suppKnown) return true;
  if (reg == 0) return false;
  int i = reg - 1;
  return suppBits[i / 8] & (0x80 >> (i % 8));
}

// ---------- current codes, cached for the dash, the FI lamp and ELM327 mode 03 ----------
struct DtcCache { uint32_t at; int n; uint8_t c[8], st[8]; };
static DtcCache dtcCur = {0, -1, {0}, {0}};

// "53 <count> <code> <status> ..." -> count, or -1 if it isn't a code reply
static int parseDtc(const uint8_t *r, int n, uint8_t *codes, uint8_t *st, int maxc) {
  if (n < 2 || r[0] != 0x53) return -1;
  int pairs = (n - 2) / 2;
  int cnt = r[1] <= pairs ? r[1] : pairs;
  for (int i = 0; i < cnt && i < maxc; i++) { codes[i] = r[2 + i * 2]; st[i] = r[3 + i * 2]; }
  return cnt;
}

static void refreshFi() {
  uint8_t rq[2] = {0x13, 0x01}, r[64];
  int n = klXfer(rq, 2, r, sizeof(r), 300);
  codesAt = millis();
  DtcCache d = {millis(), -1, {0}, {0}};
  d.n = parseDtc(r, n, d.c, d.st, 8);
  if (d.n < 0) return;
  DLOCK();
  dtcCur = d;
  live.dtc = d.n;
  live.fi = d.n > 0 ? 1 : 0;
  DUNLOCK();
}

static void setLive(int slot, float v) {
  DLOCK();
  live.v[slot] = v;
  live.ok |= 1UL << slot;
  DUNLOCK();
}
static void clearLive(int slot) {
  DLOCK();
  live.ok &= ~(1UL << slot);
  DUNLOCK();
}

static bool readAdcVbat(float &v) {
  int pin = kc.vbatPin;
  if (pin < 32 || pin > 39) return false;
  uint32_t mv = 0;
  for (int i = 0; i < 4; i++) mv += analogReadMilliVolts(pin);
  v = (mv / 4) / 1000.0f * kc.vbatMult;
  return true;
}

static void decodeReg(int r, const uint8_t *d, int n) {
  float v = 0;
  switch (r) {
    case R_RPM:
      if (n >= 2) v = kc.rpmForm == 1 ? d[0] * 100.0f + d[1] : d[0] * 100.0f + d[1] * 100.0f / 256.0f;
      else v = d[0] * 100.0f;
      break;
    case R_TPS: {
      uint16_t raw = n >= 2 ? (uint16_t)((d[0] << 8) | d[1]) : (uint16_t)(d[0] * 4);
      tpsRaw = raw;
      v = ((int)raw - kc.tpsMin) * 100.0f / (kc.tpsMax - kc.tpsMin);
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      setLive(L_TPSV, raw * 5.0f / 1024.0f);     // 10-bit, 5 V: compare with the manual's spec
      break;
    }
    case R_SPD:  v = n >= 2 ? ((d[0] << 8) | d[1]) / 2.0f : d[0]; break;
    case R_GEAR: v = d[0]; break;
    case R_ECT: case R_IAT: v = (d[0] - 48) / 1.6f; break;
    case R_MAP: case R_BARO:   // KDS2Bluetooth: 190 mmHg per volt, 5 V full scale
      v = n >= 2 ? ((d[0] << 8) | d[1]) * 950.0f / 65536.0f * 0.133322f : d[0] * 950.0f / 256.0f * 0.133322f;
      break;
    case R_VBAT: v = n >= 2 ? ((d[0] << 8) | d[1]) * 20.0f / 65536.0f : d[0] * 20.0f / 256.0f; break;
    case R_SUB:  v = n >= 2 ? ((d[0] << 8) | d[1]) * 5.0f / 1024.0f : d[0] * 5.0f / 256.0f; break;
  }
  setLive(RSLOT[r], v);
}

static bool readReg(uint8_t reg, uint8_t *d, int &n) {
  uint8_t rq[2] = {0x21, reg}, r[64];
  int m = klXfer(rq, 2, r, sizeof(r), 150);
  if (m >= 3 && r[0] == 0x61 && r[1] == reg) { n = m - 2; memcpy(d, r + 2, n); return true; }
  return false;
}

static void pollReg(int r) {
  int slot = RSLOT[r];
  uint8_t reg = kc.reg[r];
  float v;
  if (reg && isSupp(reg)) {
    uint8_t d[32];
    int n;
    if (readReg(reg, d, n)) { decodeReg(r, d, n); return; }
  }
  if (r == R_VBAT && readAdcVbat(v)) { setLive(L_VBAT, v); return; }   // no ECU value: use the divider
  clearLive(slot);
  if (r == R_TPS) clearLive(L_TPSV);
}

static void pollSig(int i) {
  Sig &s = sigs[i];
  if (!s.on) { clearLive(L_C1 + i); return; }
  uint8_t d[32];
  int n;
  float v;
  if (readReg(s.reg, d, n) && sigExtract(s, d, n, v)) { s.val = v; s.at = millis(); setLive(L_C1 + i, v); }
  else clearLive(L_C1 + i);
  // any other signal on the same register gets the same reading
  for (int j = i + 1; j < 4; j++) {
    if (sigs[j].on && sigs[j].reg == s.reg && sigExtract(sigs[j], d, n, v)) { sigs[j].val = v; sigs[j].at = millis(); }
  }
}

// One request per call: RPM, throttle and speed every cycle, then one slow item in turn.
// At ~8 requests a second that's RPM and throttle about twice a second.
static const int FASTR[] = {R_RPM, R_TPS, R_SPD};
static const int SLOWR[] = {R_GEAR, R_ECT, R_IAT, R_MAP, R_BARO, R_VBAT, R_SUB};
#define N_FAST 3
#define N_SLOW (7 + 4 + 1)       // slow registers, 4 custom signals, codes
static int lvStep = 0, lvSlow = 0;

static void finishCycle() {
  DLOCK();
  live.ms = millis();
  if ((live.ok & (1UL << L_RPM)) && live.v[L_RPM] > pkRpm) pkRpm = live.v[L_RPM];
  if ((live.ok & (1UL << L_SPD)) && live.v[L_SPD] > pkSpd) pkSpd = live.v[L_SPD];
  if ((live.ok & (1UL << L_ECT)) && live.v[L_ECT] > pkEct) pkEct = live.v[L_ECT];
  DUNLOCK();
}

static void liveStep() {
  if (lvStep < N_FAST) { pollReg(FASTR[lvStep++]); return; }
  int s = lvSlow;
  lvSlow = (lvSlow + 1) % N_SLOW;
  lvStep = 0;
  if (s < 7) pollReg(SLOWR[s]);
  else if (s < 11) { if (sigs[s - 7].on) pollSig(s - 7); else clearLive(L_C1 + s - 7); }
  else if (millis() - codesAt > 10000) refreshFi();
  finishCycle();
}

// ---------- register sweep: 21 00 .. 21 FF ----------
struct SweepE { uint8_t st, n, nrc; uint8_t d[16]; };   // st: 0 not tried, 1 data, 2 refused, 3 no reply
static SweepE sweep[256];
static volatile int swPos = -1;      // -1 never run, 0..255 running, 256 done
static uint32_t swStart = 0, swEnd = 0;

static void sweepStep() {
  if (swPos < 0 || swPos > 255) { kMode = KM_LIVE; return; }
  int reg = swPos;
  uint8_t rq[2] = {0x21, (uint8_t)reg}, r[64];
  int n = klXfer(rq, 2, r, sizeof(r), 150);
  SweepE e = {};
  if (n >= 2 && r[0] == 0x61) { e.st = 1; e.n = n - 2 > 16 ? 16 : n - 2; memcpy(e.d, r + 2, e.n); }
  else if (n >= 3 && r[0] == 0x7F) { e.st = 2; e.nrc = r[2]; }
  else e.st = 3;
  DLOCK();
  sweep[reg] = e;
  DUNLOCK();
  swPos = reg + 1;
  if (swPos > 255) { swEnd = millis(); kMode = KM_LIVE; trNote("sweep done"); }
}

// ---------- register monitor: re-reads a list of registers, tracks which bytes move ----------
#define MON_MAX 48
struct MonE { uint8_t r, n, nrc; bool ok; uint8_t d[12], lo[12], hi[12]; uint16_t chg, mk; uint32_t cnt, last; };
static MonE mon[MON_MAX];
static volatile int monN = 0;
static int monIdx = 0;
static uint32_t monStart = 0;

static void monStep() {
  int n = monN;
  if (n <= 0) { kMode = KM_LIVE; return; }
  int i = monIdx % n;
  monIdx = (monIdx + 1) % n;
  uint8_t rq[2] = {0x21, mon[i].r}, r[64];
  int m = klXfer(rq, 2, r, sizeof(r), 150);
  DLOCK();
  MonE &e = mon[i];
  if (m >= 2 && r[0] == 0x61) {
    int dn = m - 2 > 12 ? 12 : m - 2;
    for (int b = 0; b < dn; b++) {
      uint8_t v = r[2 + b];
      if (!e.cnt || b >= e.n) { e.lo[b] = e.hi[b] = v; }
      else if (v != e.d[b]) { e.chg |= 1 << b; e.mk |= 1 << b; }
      if (v < e.lo[b]) e.lo[b] = v;
      if (v > e.hi[b]) e.hi[b] = v;
      e.d[b] = v;
    }
    e.n = dn;
    e.ok = true;
    e.cnt++;
    e.last = millis();
  } else if (m >= 3 && r[0] == 0x7F) { e.ok = false; e.nrc = r[2]; }
  DUNLOCK();
}

static void onConnected() {
  memset(suppBits, 0, sizeof(suppBits));
  bool any = false;
  for (int base = 0; base <= 0xE0; base += 0x20) {
    uint8_t rq[2] = {0x21, (uint8_t)base}, r[16];
    int n = klXfer(rq, 2, r, sizeof(r), 150);
    if (n < 6 || r[0] != 0x61 || r[1] != base) break;
    for (int b = 0; b < 4; b++) suppBits[base / 8 + b] = r[2 + b];
    any = true;
    if (!(r[5] & 1)) break;               // last bit = next block exists
  }
  suppKnown = any;
  uint8_t rq[1] = {0x1A}, r[48];
  int n = klXfer(rq, 1, r, sizeof(r), 300);
  char id[32] = "";
  if (n >= 2 && r[0] == 0x5A) {
    int k = 0;
    for (int i = 1; i < n && k < 31; i++) if (r[i] >= 0x20 && r[i] < 0x7F) id[k++] = r[i];
    id[k] = 0;
  }
  DLOCK();
  memcpy(ecuId, id, sizeof(id));
  DUNLOCK();
  ecuFound = true;
  connectedAt = millis();
  refreshFi();
  char note[48];
  snprintf(note, sizeof(note), "ECU %s", id[0] ? id : "connected");
  trNote(note);
}

static void klIdle() { delay(10); }

static void klService() {
  if (jobState == 1) {
    job.n = klRequest(job.tgt, job.req, job.len, job.resp, sizeof(job.resp), job.to);
    jobState = 2;
#if K_TASK
    xSemaphoreGive(jobDone);
#endif
    return;
  }
  if (!ks.session || ks.target != kc.ecu) {
    if (ks.session) klClose();                  // after an ABS job: back to the engine ECU
    if ((int32_t)(millis() - kNextOpen) < 0) { klIdle(); return; }
    if (klOpen(kc.ecu)) { kOpenFails = 0; onConnected(); }
    else {
      ecuFound = false;
      kOpenFails++;
      kNextOpen = millis() + (kOpenFails < 3 ? 2000 : kOpenFails < 6 ? 5000 : 10000);
      DLOCK();
      live.ok &= (1UL << L_VBAT);
      DUNLOCK();
      float v;
      if (readAdcVbat(v)) setLive(L_VBAT, v);
    }
    return;
  }
  if (!ecuFound) { onConnected(); return; }
  if (kMode == KM_SWEEP) { sweepStep(); return; }
  if (kMode == KM_MON) { monStep(); return; }
  if ((int32_t)(liveWantUntil - millis()) > 0) { liveStep(); return; }
  if (millis() - codesAt > 15000) { refreshFi(); return; }
  klKeepAlive();
  klIdle();
}

#if K_TASK
static void klTask(void *) {
  for (;;) klService();
}
#endif

// =====================================================================
//  Live JSON + logging (loop side)
// =====================================================================
static uint32_t lastSerialLive = 0;

static LiveData liveSnap() {
  LiveData d;
  DLOCK();
  d = live;
  DUNLOCK();
  return d;
}

static const char *modeName() { return kMode == KM_SWEEP ? "sweep" : kMode == KM_MON ? "mon" : "live"; }

static String liveJson() {
  LiveData d = liveSnap();
  String o = "{\"t\":\"live\",\"ms\":" + String(d.ms) + ",\"ecu\":" + (ecuFound ? "true" : "false");
  for (int i = 0; i < L_N; i++) {
    o += ",\"" + String(LKEY[i]) + "\":";
    o += (d.ok & (1UL << i)) ? fmtF(d.v[i], LDP[i]) : String("null");
  }
  o += ",\"fi\":" + String(d.fi) + ",\"dtc\":" + String(d.dtc);
  o += ",\"pk\":{\"rpm\":" + String((int)pkRpm) + ",\"spd\":" + String((int)pkSpd) +
       ",\"ect\":" + (pkEct > -100 ? String((int)pkEct) : String("null")) + "}";
  o += ",\"mode\":\"" + String(modeName()) + "\",\"rate\":" + fmtF(ks.rate, 1);
  return o + ",\"log\":" + String(logging ? "true" : "false") + "}";
}

// Log buffer, allocated when logging starts: up to LOG_BIG samples if that still leaves
// LOG_SPARE bytes for Wi-Fi / Bluetooth, else LOG_N.
#define LOG_N 600
#define LOG_BIG 1500
#define LOG_SPARE 60000
struct LogRec { uint32_t ms; int16_t v[L_N]; uint32_t ok; };
static LogRec *logBuf = nullptr;
static int logMax = LOG_N;
static int logHead = 0, logCount = 0;
static uint32_t lastLogMs = 0, logStartMs = 0;

static bool logAlloc() {
  if (logBuf) return true;
  const int sizes[] = {LOG_BIG, LOG_N};
  for (int n : sizes) {
    if (ESP.getFreeHeap() < (size_t)n * sizeof(LogRec) + LOG_SPARE && n != LOG_N) continue;
    logBuf = (LogRec *)calloc(n, sizeof(LogRec));
    if (logBuf) { logMax = n; return true; }
  }
  return false;
}

static void logSample() {
  if (!logAlloc()) { logging = false; return; }
  LiveData d = liveSnap();
  LogRec &r = logBuf[logHead];
  r.ms = millis() - logStartMs;
  r.ok = d.ok;
  for (int i = 0; i < L_N; i++) {
    float s = d.v[i] * LSCALE[i];
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    r.v[i] = (int16_t)s;
  }
  logHead = (logHead + 1) % logMax;
  if (logCount < logMax) logCount++;
}

static String logHeader() {
  String h = "seconds,rpm,tps_pct,tps_v,subthrottle_v,map_kpa,baro_kpa,speed_kmh,coolant_c,intake_c,battery_v,gear";
  for (int i = 0; i < 4; i++) {
    String n = sigs[i].on ? sigs[i].name : String("signal") + (i + 1);
    n.replace(",", " ");
    if (sigs[i].on && sigs[i].unit.length()) n += "_" + sigs[i].unit;
    h += "," + n;
  }
  return h + "\n";
}

static String logRow(int idx) {
  if (!logBuf) return "";
  const LogRec &r = logBuf[idx];
  String s = String((float)(r.ms / 1000.0f), 1U);
  for (int i = 0; i < L_N; i++) {
    s += ',';
    if (r.ok & (1UL << i)) s += String((float)(r.v[i] / LSCALE[i]), (unsigned int)LDP[i]);
  }
  s += '\n';
  return s;
}

static int logIndex(int n) { return (logHead - logCount + n + logMax) % logMax; }

// =====================================================================
//  Commands (shared by web, app, serial)
// =====================================================================
enum Chan : uint8_t { CH_SERIAL, CH_WEB, CH_BT };
static uint32_t btAppAt = 0;     // last time the OBDojo app talked over Bluetooth

static String codeListJson(const uint8_t *r, int n) {
  uint8_t c[16], st[16];
  int cnt = parseDtc(r, n, c, st, 16);
  if (cnt < 0) return "null";
  String o = "[";
  for (int i = 0; i < cnt && i < 16; i++) {
    if (i) o += ",";
    o += "{\"code\":\"" + hex2(c[i]) + "\",\"desc\":" + q(svcDesc(c[i])) + ",\"st\":\"" + hex2(st[i]) + "\"}";
  }
  return o + "]";
}

static String cmdCodes() {
  if (!ecuFound) return errJson("codes", "ECU not connected. Key on?");
  uint8_t r[64], rq[2];
  String o = "{\"t\":\"codes\",\"ok\":true";
  rq[0] = 0x21; rq[1] = 0x56;
  int n = ecuReq(rq, 2, r, sizeof(r), 400);
  o += ",\"count\":" + String(n >= 3 && r[0] == 0x61 && r[1] == 0x56 ? String(r[2]) : String("null"));
  rq[0] = 0x13; rq[1] = 0x01;
  n = ecuReq(rq, 2, r, sizeof(r), 600);
  o += ",\"current\":" + codeListJson(r, n) + ",\"raw_cur\":\"" + (n > 0 ? toHex(r, n) : String("")) + "\"";
  if (n > 0) {
    DtcCache d = {millis(), -1, {0}, {0}};
    d.n = parseDtc(r, n, d.c, d.st, 8);
    if (d.n >= 0) {
      DLOCK();
      dtcCur = d;
      live.dtc = d.n;
      live.fi = d.n > 0 ? 1 : 0;
      DUNLOCK();
    }
  }
  rq[1] = 0x02;
  n = ecuReq(rq, 2, r, sizeof(r), 600);
  o += ",\"stored\":" + codeListJson(r, n) + ",\"raw_sto\":\"" + (n > 0 ? toHex(r, n) : String("")) + "\"";
  o += ",\"fi\":" + String(live.fi == 1 ? "true" : "false");
  return o + "}";
}

static String cmdClear(const String &arg) {
  if (arg != "yes") return errJson("clear", "Send 'clear yes' to confirm");
  if (!ecuFound) return errJson("clear", "ECU not connected. Key on?");
  uint8_t rq[2] = {0x14, 0x01}, r[16];
  int n = ecuReq(rq, 2, r, sizeof(r), 3000);
  if (n == 0) return errJson("clear", "No response, so the clear may not have worked");
  if (r[0] == 0x7F) return errJson("clear", String("ECU refused: ") + (n >= 3 ? nrcText(r[2]) : "?"));
  if (r[0] != 0x54) return errJson("clear", "Unexpected reply " + toHex(r, n));
  DLOCK();
  live.fi = 0; live.dtc = 0; dtcCur.n = 0; dtcCur.at = millis();
  DUNLOCK();
  return "{\"t\":\"clear\",\"ok\":true,\"msg\":\"Codes cleared. Cycle the key and check dealer mode.\"}";
}

// Kawasaki failure records: count in register 56, then 12 <n> 01 54 for each (raw, decoding unconfirmed)
static String cmdFreeze() {
  if (!ecuFound) return errJson("freeze", "ECU not connected");
  uint8_t rq[4] = {0x21, 0x56}, r[64];
  int n = ecuReq(rq, 2, r, sizeof(r), 400);
  if (n < 3 || r[0] != 0x61) return errJson("freeze", "ECU didn't give a failure count (register 56)");
  int cnt = r[2];
  String o = "{\"t\":\"freeze\",\"ok\":true,\"count\":" + String(cnt) + ",\"list\":[";
  for (int i = 1; i <= cnt && i <= 8; i++) {
    uint8_t fq[4] = {0x12, (uint8_t)i, 0x01, 0x54};
    int m = ecuReq(fq, 4, r, sizeof(r), 500);
    if (i > 1) o += ",";
    o += "{\"n\":" + String(i) + ",\"hex\":\"" + (m > 0 ? toHex(r, m) : String("no reply")) + "\"";
    if (m >= 4 && r[0] == 0x52) o += ",\"code\":\"" + hex2(r[m >= 5 ? 3 : 2]) + "\"";
    o += "}";
  }
  return o + "]}";
}

static String suppList() {
  String o;
  for (int reg = 1; reg <= 0xFF; reg++) {
    if (reg % 0x20 == 0) continue;     // the bitmap registers themselves
    int i = reg - 1;
    if (suppBits[i / 8] & (0x80 >> (i % 8))) { if (o.length()) o += " "; o += hex2(reg); }
  }
  return o;
}

static String itemJson(const String &k, const String &v) { return "{\"k\":" + q(k) + ",\"v\":" + q(v) + "}"; }

static String cmdIdent() {
  if (!ecuFound) return errJson("ident", "ECU not connected. Key on?");
  String o = "{\"t\":\"ident\",\"ok\":true,\"items\":[";
  char id[32];
  DLOCK();
  memcpy(id, ecuId, sizeof(id));
  DUNLOCK();
  o += itemJson("ECU part number (1A)", id[0] ? id : "not given");
  o += "," + itemJson("Keywords", hex2(ks.kb1) + " " + hex2(ks.kb2));
  o += "," + itemJson("Tester address", hex2(ks.tester));
  o += "," + itemJson("ECU address", hex2(ks.target));
  o += "," + itemJson("Supported registers", suppKnown ? suppList() : String("ECU didn't send the list"));
  uint8_t rq[2] = {0x21, 0x44}, r[32];
  int n = ecuReq(rq, 2, r, sizeof(r), 400);
  if (n >= 6 && r[0] == 0x61) {
    uint32_t secs = ((uint32_t)r[2] << 24) | ((uint32_t)r[3] << 16) | ((uint32_t)r[4] << 8) | r[5];
    o += "," + itemJson("Engine run time (21 44)", fmtF(secs / 3600.0f, 1) + " h (" + String(secs) + " s, unconfirmed)");
  }
  return o + "]}";
}

static String cmdSweep(const String &a) {
  if (a == "start") {
    if (!ecuFound) return errJson("sweep", "ECU not connected. Key on?");
    DLOCK();
    memset(sweep, 0, sizeof(sweep));
    DUNLOCK();
    swStart = millis();
    swEnd = 0;
    swPos = 0;
    kMode = KM_SWEEP;
  } else if (a == "stop") {
    if (kMode == KM_SWEEP) kMode = KM_LIVE;
  }
  int pos = swPos;
  String o = "{\"t\":\"sweep\",\"ok\":true,\"on\":" + String(kMode == KM_SWEEP ? "true" : "false") +
             ",\"pos\":" + String(pos) + ",\"done\":" + (pos > 255 ? "true" : "false");
  o += ",\"ms\":" + String(pos < 0 ? 0 : (swEnd ? swEnd : millis()) - swStart);
  int refused = 0, silent = 0, found = 0;
  String items = "[";
  for (int reg = 0; reg < 256; reg++) {
    SweepE e;
    DLOCK();
    e = sweep[reg];
    DUNLOCK();
    if (e.st == 2) refused++;
    if (e.st == 3) silent++;
    if (e.st != 1) continue;
    if (found++) items += ",";
    items += "{\"r\":\"" + hex2(reg) + "\",\"nm\":" + q(regName(reg)) + ",\"hex\":\"" + toHex(e.d, e.n) + "\"}";
  }
  o += ",\"found\":" + String(found) + ",\"refused\":" + String(refused) + ",\"silent\":" + String(silent);
  return o + ",\"items\":" + items + "]}";
}

static void monAdd(uint8_t reg) {
  if (monN >= MON_MAX) return;
  for (int i = 0; i < monN; i++) if (mon[i].r == reg) return;
  MonE e = {};
  e.r = reg;
  mon[monN] = e;
  monN = monN + 1;
}

static String cmdMon(String a) {
  a.trim();
  if (a.startsWith("start")) {
    if (!ecuFound) return errJson("mon", "ECU not connected. Key on?");
    String rest = a.substring(5);
    rest.trim();
    kMode = KM_LIVE;
    DLOCK();
    monN = 0;
    DUNLOCK();
    if (!rest.length() || rest == "all") {
      bool swept = swPos > 255;
      for (int reg = 1; reg < 256; reg++) {
        if (reg % 0x20 == 0) continue;
        if (swept ? sweep[reg].st == 1 : isSupp(reg)) monAdd(reg);
      }
      if (!monN) for (int r = 0; r < R_N; r++) if (kc.reg[r]) monAdd(kc.reg[r]);
    } else {
      uint8_t regs[MON_MAX];
      int n = parseHex(rest, regs, MON_MAX);
      if (!n) return errJson("mon", "Give registers in hex, e.g. mon start 04 05 07 5B");
      for (int i = 0; i < n; i++) monAdd(regs[i]);
    }
    monIdx = 0;
    monStart = millis();
    kMode = KM_MON;
  } else if (a == "stop") {
    if (kMode == KM_MON) kMode = KM_LIVE;
  } else if (a == "mark") {
    DLOCK();
    for (int i = 0; i < monN; i++) { mon[i].mk = 0; memcpy(mon[i].lo, mon[i].d, 12); memcpy(mon[i].hi, mon[i].d, 12); }
    DUNLOCK();
  }
  String o = "{\"t\":\"mon\",\"ok\":true,\"on\":" + String(kMode == KM_MON ? "true" : "false") +
             ",\"ms\":" + String(kMode == KM_MON ? millis() - monStart : 0) + ",\"rate\":" + fmtF(ks.rate, 1) + ",\"regs\":[";
  for (int i = 0; i < monN; i++) {
    MonE e;
    DLOCK();
    e = mon[i];
    mon[i].chg = 0;
    DUNLOCK();
    if (i) o += ",";
    o += "{\"r\":\"" + hex2(e.r) + "\",\"nm\":" + q(regName(e.r)) + ",\"n\":" + String(e.cnt);
    if (e.cnt) {
      o += ",\"d\":\"" + toHex(e.d, e.n) + "\",\"lo\":\"" + toHex(e.lo, e.n) + "\",\"hi\":\"" + toHex(e.hi, e.n) +
           "\",\"c\":" + String(e.chg) + ",\"m\":" + String(e.mk) + ",\"age\":" + String(millis() - e.last);
    }
    if (!e.ok && e.nrc) o += ",\"nrc\":" + q(nrcText(e.nrc));
    o += "}";
  }
  return o + "]}";
}

// "[@28] 21 04": read-only request for the watch tool
static bool parseTarget(String &a, uint8_t &tgt) {
  a.trim();
  tgt = ecuAddr();
  if (!a.startsWith("@")) return true;
  int sp = a.indexOf(' ');
  if (sp < 2) return false;
  String t = a.substring(1, sp);
  if (t.length() != 2 || !isxdigit(t[0]) || !isxdigit(t[1])) return false;
  tgt = strtoul(t.c_str(), nullptr, 16);
  a = a.substring(sp + 1);
  return true;
}

static String cmdPeek(String a) {
  uint8_t tgt;
  if (!parseTarget(a, tgt)) return errJson("peek", "Use @XX then the request, e.g. @28 1A");
  uint8_t rq[16];
  int len = parseHex(a, rq, 15);
  if (!len) return errJson("peek", "Give a request in hex, e.g. 21 04");
  const uint8_t okSvc[] = {0x21, 0x1A, 0x12, 0x13, 0x17, 0x18, 0x3E};
  bool allowed = false;
  for (uint8_t s : okSvc) if (rq[0] == s) allowed = true;
  if (!allowed) return errJson("peek", "Watch only allows read requests (21 1A 12 13 17 18 3E)");
  uint8_t r[260];
  int n = kJob(tgt, rq, len, r, sizeof(r), 300);
  if (n == 0) return errJson("peek", "No reply");
  String o = "{\"t\":\"peek\",\"ok\":true,\"req\":\"" + toHex(rq, len) + "\",\"hex\":\"" + toHex(r, n) + "\"";
  if (r[0] == 0x7F && n >= 3) o += ",\"nrc\":" + q(nrcText(r[2]));
  return o + "}";
}

static String klJson() {
  KStats s;
  DLOCK();
  s = ks;
  DUNLOCK();
  String o = "\"session\":" + String(s.session ? "true" : "false") + ",\"tester\":\"" + hex2(s.tester) +
             "\",\"target\":\"" + hex2(s.target) + "\",\"kb\":\"" + hex2(s.kb1) + " " + hex2(s.kb2) +
             "\",\"init_ms\":" + fmtF(s.initLowMs, 1) + ",\"echo\":" + String(s.echo) +
             ",\"req\":" + String(s.req) + ",\"okc\":" + String(s.ok) + ",\"tmo\":" + String(s.tmo) +
             ",\"nrc\":" + String(s.nrc) + ",\"cks\":" + String(s.cks) + ",\"echo_bad\":" + String(s.echoBad) +
             ",\"inits\":" + String(s.inits) + ",\"init_ok\":" + String(s.initOk) + ",\"rate\":" + fmtF(s.rate, 1) +
             ",\"err\":" + q(s.err) + ",\"mode\":\"" + modeName() + "\"";
  return o;
}

static String cmdKline() {
  String o = "{\"t\":\"kline\",\"ok\":true," + klJson() + ",\"trace\":[";
  TraceE tr[TR_N];
  int cnt, head;
  DLOCK();
  memcpy(tr, trace, sizeof(trace));
  cnt = trCount;
  head = trHead;
  DUNLOCK();
  for (int i = 0; i < cnt; i++) {
    const TraceE &e = tr[(head - cnt + i + TR_N) % TR_N];
    if (i) o += ",";
    o += "{\"ms\":" + String(e.ms) + ",\"d\":\"" + String(e.dir) + "\",";
    if (e.dir == 'I') { String t; for (int k = 0; k < e.n; k++) t += (char)e.d[k]; o += "\"x\":" + q(t) + "}"; }
    else o += "\"x\":\"" + toHex(e.d, e.n) + "\"}";
  }
  return o + "]}";
}

// Fresh connection attempt with a step-by-step report, for checking the wiring on the bike
static String cmdKtest() {
  kForceReopen = true;
  uint8_t rq[2] = {0x21, kc.reg[R_RPM] ? kc.reg[R_RPM] : (uint8_t)0x09}, r[32];
  int n = ecuReq(rq, 2, r, sizeof(r), 300);
  KStats s;
  DLOCK();
  s = ks;
  DUNLOCK();
  auto step = [](const char *k, int ok, const String &m) {
    return "{\"k\":" + q(k) + ",\"ok\":" + String(ok < 0 ? "null" : ok ? "true" : "false") + ",\"msg\":" + q(m) + "}";
  };
  String o = "{\"t\":\"ktest\",\"ok\":true,\"steps\":[";
  bool pulseOk = s.initLowMs >= 23.5f;
  o += step("Wake-up pulse on the bus", pulseOk, s.initLowMs < 0.5f ? String("Bus never went low") :
            fmtF(s.initLowMs, 1) + " ms low" + (pulseOk ? "" : " (needs 25: the transceiver cut it short)"));
  o += "," + step("Transceiver echo", s.echo, s.echo == 1 ? String("Our bytes came back on RX") : String("Nothing came back: check RXD, pull-up, CS, VBB"));
  o += "," + step("ECU answered", s.session ? 1 : 0, s.session ? "Keywords " + hex2(s.kb1) + " " + hex2(s.kb2) + ", tester " + hex2(s.tester) : String(s.err));
  o += "," + step("Read a register", n >= 2 && r[0] == 0x61 ? 1 : 0, n > 0 ? "21 " + hex2(rq[1]) + " -> " + toHex(r, n) : String("No reply"));
  return o + "]," + klJson() + "}";
}

// ABS ECU (address 28) on the same K-line. Unconfirmed on the EX300B: its ABS may only talk on
// its own connector.
static String cmdAbs() {
  uint8_t rq[2] = {0x1A, 0}, r[64];
  String o = "{\"t\":\"abs\",\"ok\":true";
  int n = kJob(0x28, rq, 1, r, sizeof(r), 400);
  if (n == 0) return errJson("abs", String("ABS ECU (28) didn't answer on this plug. ") + ks.err);
  o += ",\"id\":" + q(r[0] == 0x5A ? toAscii(r + 1, n - 1) : toHex(r, n));
  rq[0] = 0x13; rq[1] = 0x01;
  n = kJob(0x28, rq, 2, r, sizeof(r), 600);
  o += ",\"current\":" + codeListJson(r, n) + ",\"raw_cur\":\"" + (n > 0 ? toHex(r, n) : String("")) + "\"";
  rq[1] = 0x02;
  n = kJob(0x28, rq, 2, r, sizeof(r), 600);
  o += ",\"stored\":" + codeListJson(r, n) + ",\"raw_sto\":\"" + (n > 0 ? toHex(r, n) : String("")) + "\"";
  return o + ",\"msg\":\"ABS code numbers aren't the engine's DFI codes; check the ABS section of the manual.\"}";
}

static void applyKCfg();
static String cmdTps(const String &a) {
  if (a == "closed" || a == "open") {
    if (!ecuFound) return errJson("tps", "ECU not connected. Key on, engine off");
    uint8_t rq[2] = {0x21, kc.reg[R_TPS]}, r[16];
    if (!rq[1]) return errJson("tps", "Throttle register is turned off in Settings");
    int n = ecuReq(rq, 2, r, sizeof(r), 300);
    if (n < 3 || r[0] != 0x61) return errJson("tps", "No throttle reading");
    int raw = n >= 4 ? (r[2] << 8) | r[3] : r[2] * 4;
    int k = a == "closed" ? K_TPS_MIN : K_TPS_MAX;
    if (a == "closed" && raw >= sInt(K_TPS_MAX)) return errJson("tps", "Closed reading is above the open value. Learn open first or reset");
    if (a == "open" && raw <= sInt(K_TPS_MIN) + 20) return errJson("tps", "That's barely above closed. Hold the throttle wide open (engine off)");
    sv[k] = String(raw);
    saveSetting(k);
    applyKCfg();
  } else if (a == "reset") {
    sv[K_TPS_MIN] = SDEFS[K_TPS_MIN].def; saveSetting(K_TPS_MIN);
    sv[K_TPS_MAX] = SDEFS[K_TPS_MAX].def; saveSetting(K_TPS_MAX);
    applyKCfg();
  }
  return "{\"t\":\"tps\",\"ok\":true,\"min\":" + String(kc.tpsMin) + ",\"max\":" + String(kc.tpsMax) + ",\"raw\":" + String(tpsRaw) + "}";
}

// Services that can write to or reprogram the ECU are never sent, raw or not
static bool blockedService(const uint8_t *rq, int len) {
  uint8_t s = rq[0];
  if (s == 0x11 || s == 0x27 || s == 0x2E || s == 0x3B || s == 0x3D || s == 0x85) return true;
  if (s >= 0x31 && s <= 0x3A) return true;          // routines, download / upload, transfer
  if (s == 0x10 && len > 1 && rq[1] == 0x85) return true;   // programming session
  return false;
}

static String cmdRaw(String a) {
  if (!sBool(K_ALLOW_RAW)) return errJson("raw", "Raw commands are off (Settings > K-line / ECU)");
  uint8_t tgt;
  if (!parseTarget(a, tgt)) return errJson("raw", "Use @XX then the request, e.g. @28 1A");
  uint8_t rq[64];
  int len = parseHex(a, rq, 63);
  if (len == 0) return errJson("raw", "Give hex bytes, e.g. raw 21 04");
  if (blockedService(rq, len)) return errJson("raw", "Blocked: that service can write to or reprogram the ECU");
  uint8_t r[260];
  int n = kJob(tgt, rq, len, r, sizeof(r), 1500);
  if (n == 0) return errJson("raw", "No reply");
  return "{\"t\":\"raw\",\"ok\":true,\"req\":\"" + toHex(rq, len) + "\",\"hex\":\"" + toHex(r, n) +
         "\",\"ascii\":" + q(toAscii(r, n)) + "}";
}

static String cmdStatus(Chan ch) {
  (void)ch;
  String o = "{\"t\":\"status\",\"ok\":true,\"fw\":\"" FW_VERSION "\",\"bike\":\"EX300\"";
  o += ",\"ecu\":" + String(ecuFound ? "true" : "false");
  char id[32];
  DLOCK();
  memcpy(id, ecuId, sizeof(id));
  DUNLOCK();
  o += ",\"ecu_id\":" + q(id);
  o += ",\"kl\":{" + klJson() + "}";
  o += ",\"fi\":" + String(live.fi) + ",\"dtc\":" + String(live.dtc);
  o += ",\"uptime\":" + String(millis() / 1000);
  o += ",\"heap\":" + String(ESP.getFreeHeap());
  o += ",\"tracker\":" + String(trkStarted && SerialBT.hasClient() ? "true" : "false");
  {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char b[20];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    o += ",\"bt_mac\":\"" + String(b) + "\"";
  }
  o += ",\"wifi_clients\":" + String(WiFi.softAPgetStationNum());
  o += ",\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"";
  o += ",\"sta_ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\"";
  o += ",\"log\":" + String(logging ? "true" : "false") + ",\"log_n\":" + String(logCount) + ",\"log_max\":" + String(logMax);
  o += ",\"units\":" + String(sInt(K_UNITS)) + ",\"live_ms\":" + String(sInt(K_LIVE_MS));
  o += ",\"temp_warn\":" + String(sInt(K_TEMP_WARN)) + ",\"rpm_warn\":" + String(sInt(K_RPM_WARN));
  o += ",\"vbat_low\":" + fmtF(sFlt(K_VBAT_LOW), 1);
  o += ",\"raw\":" + String(sBool(K_ALLOW_RAW) ? "true" : "false");
  o += ",\"bt_client\":" + String(trkStarted && SerialBT.hasClient() ? "true" : "false");
  o += ",\"bt_mode\":\"" + String(!(trkStarted && SerialBT.hasClient()) ? "none" : (btAppAt && millis() - btAppAt < 10000 ? "app" : "elm")) + "\"";
  o += ",\"tps\":{\"min\":" + String(kc.tpsMin) + ",\"max\":" + String(kc.tpsMax) + ",\"raw\":" + String(tpsRaw) + "}";
  o += ",\"regs\":" + q(sStr(K_REGS)) + ",\"supp\":" + (suppKnown ? q(suppList()) : String("null"));
  o += ",\"sweep\":" + String(swPos);
  o += ",\"sigs\":[";
  bool first = true;
  for (int i = 0; i < 4; i++) {
    if (!sigs[i].on) continue;
    if (!first) o += ",";
    first = false;
    o += "{\"k\":\"c" + String(i + 1) + "\",\"n\":" + q(sigs[i].name) + ",\"u\":" + q(sigs[i].unit) + "}";
  }
  o += "]";
  return o + "}";
}

static const char *typeName(SType t) {
  switch (t) {
    case S_STR: return "str"; case S_PASS: return "pass"; case S_INT: return "int";
    case S_BOOL: return "bool"; case S_FLOAT: return "float"; default: return "sel";
  }
}

static String cmdSettings() {
  String o = "{\"t\":\"settings\",\"ok\":true,\"fields\":[";
  for (int i = 0; i < K_COUNT; i++) {
    const SDef &d = SDEFS[i];
    if (i) o += ",";
    o += "{\"k\":\"" + String(d.key) + "\",\"l\":" + q(d.label) + ",\"g\":" + q(d.group) +
         ",\"t\":\"" + typeName(d.type) + "\",\"r\":" + (d.reboot ? "true" : "false") +
         ",\"min\":" + String(d.mn) + ",\"max\":" + String(d.mx);
    if (d.type == S_PASS) o += ",\"v\":\"\",\"set\":" + String(sv[i].length() ? "true" : "false");
    else o += ",\"v\":" + q(sv[i]);
    if (d.type == S_SEL) o += ",\"o\":" + q(d.opts);
    o += "}";
  }
  return o + "]}";
}

static String cmdSet(const String &args) {
  int sp = args.indexOf(' ');
  String key = sp < 0 ? args : args.substring(0, sp);
  String val = sp < 0 ? "" : args.substring(sp + 1);
  val.trim();
  int k = -1;
  for (int i = 0; i < K_COUNT; i++) if (key == SDEFS[i].key) { k = i; break; }
  if (k < 0) return errJson("set", "Unknown setting " + key);
  const SDef &d = SDEFS[k];

  switch (d.type) {
    case S_INT: case S_SEL: {
      if (!val.length()) return errJson("set", String(d.label) + ": value needed");
      for (size_t i = 0; i < val.length(); i++)
        if (!isdigit(val[i]) && !(i == 0 && val[i] == '-')) return errJson("set", String(d.label) + ": whole number needed");
      long n = val.toInt();
      if (n < d.mn || n > d.mx) return errJson("set", String(d.label) + ": must be " + String((int)d.mn) + "-" + String((int)d.mx));
      val = String(n);
      break;
    }
    case S_FLOAT: {
      float f = val.toFloat();
      if (!val.length() || f < d.mn || f > d.mx) return errJson("set", String(d.label) + ": must be " + String(d.mn, 1U) + "-" + String(d.mx, 1U));
      val = String(f, 2U);
      break;
    }
    case S_BOOL: {
      String v = val; v.toLowerCase();
      val = (v == "1" || v == "true" || v == "on" || v == "yes") ? "1" : "0";
      break;
    }
    case S_STR: case S_PASS: {
      if (val.length() < (size_t)d.mn && !(d.type == S_PASS && val.length() == 0))
        return errJson("set", String(d.label) + ": at least " + String((int)d.mn) + " characters");
      if (val.length() > (size_t)d.mx) return errJson("set", String(d.label) + ": max " + String((int)d.mx) + " characters");
      if (k == K_AP_PASS && val.length() > 0 && val.length() < 8) return errJson("set", "Hotspot password must be 8+ characters (or blank)");
      if (k == K_TRK_PIN) for (size_t i = 0; i < val.length(); i++) if (!isdigit(val[i])) return errJson("set", "PIN must be digits");
      if (k == K_HOSTNAME) for (size_t i = 0; i < val.length(); i++)
        if (!isalnum(val[i]) && val[i] != '-') return errJson("set", "Network name: letters, numbers and - only");
      break;
    }
  }
  if (k == K_AP_IP) {
    IPAddress ip;
    if (!ip.fromString(val) || ip[0] == 0 || ip[0] == 127 || ip[0] >= 224 || ip[3] == 0 || ip[3] == 255)
      return errJson("set", "Hotspot address must be a normal IP like 1.1.1.1 or 10.1.1.1");
  }
  if (k == K_KL_ECU) {
    val.toUpperCase();
    if (!isxdigit(val[0]) || !isxdigit(val[1])) return errJson("set", "ECU address must be 2 hex digits, e.g. 11");
  }
  if (k == K_REGS) {
    uint8_t tmp[R_N];
    String e = regsParse(val, tmp);
    if (e.length()) return errJson("set", "Live value registers: " + e);
    if (!val.length()) val = REGS_DEF;
  }
  if (k >= K_SIG1 && k <= K_SIG4) {
    Sig tmp;
    String e = sigParse(val, tmp);
    if (e.length()) return errJson("set", String("Signal ") + (k - K_SIG1 + 1) + ": " + e);
  }
  if (k == K_VBAT_PIN && val.toInt() != 0 && (val.toInt() < 32 || val.toInt() > 39))
    return errJson("set", "Battery pin must be 32-39 (ADC1) or 0");
  if ((k == K_KL_TX || k == K_KL_RX || k == K_KL_CS || k == K_KL_INIT || k == K_LED_PIN) && (val.toInt() >= 6 && val.toInt() <= 11))
    return errJson("set", "Pins 6-11 are used by the flash chip");
  if ((k == K_KL_TX || k == K_KL_CS || k == K_KL_INIT) && val.toInt() >= 34)
    return errJson("set", "Pins 34-39 are input only");

  sv[k] = val;
  saveSetting(k);
  if (k >= K_SIG1 && k <= K_SIG4) sigParse(val, sigs[k - K_SIG1]);
  applyKCfg();
  return "{\"t\":\"set\",\"ok\":true,\"k\":\"" + key + "\",\"reboot\":" + (d.reboot ? "true" : "false") + "}";
}

// Log download in chunks for the Bluetooth app: "logrows <start> <count>"
static String cmdLogRows(const String &a) {
  int sp = a.indexOf(' ');
  int start = max(0L, a.toInt());
  int cnt = sp < 0 ? 40 : constrain(a.substring(sp + 1).toInt(), 1L, 80L);
  String rows;
  int n = 0;
  for (int i = start; i < logCount && n < cnt; i++, n++) rows += logRow(logIndex(i));
  String o = "{\"t\":\"logrows\",\"ok\":true,\"total\":" + String(logCount) + ",\"start\":" + String(start) + ",\"n\":" + String(n);
  if (start == 0) o += ",\"header\":" + q(logHeader());
  return o + ",\"rows\":" + q(rows) + "}";
}

static String cmdLog(const String &arg) {
  if (arg == "start") {
    if (!logAlloc()) return errJson("log", "Not enough memory to log");
    if (!logging) { logging = true; logStartMs = millis(); logHead = 0; logCount = 0; }
  } else if (arg == "stop") {
    logging = false;
  } else if (arg == "clear") {
    logHead = 0; logCount = 0;
  }
  return "{\"t\":\"log\",\"ok\":true,\"on\":" + String(logging ? "true" : "false") + ",\"n\":" + String(logCount) +
         ",\"max\":" + String(logMax) + ",\"ms\":" + String(sInt(K_LOG_MS)) + "}";
}

static String runCommand(String line, Chan ch) {
  line.trim();
  int sp = line.indexOf(' ');
  String c = sp < 0 ? line : line.substring(0, sp);
  String a = sp < 0 ? "" : line.substring(sp + 1);
  c.toLowerCase();
  a.trim();

  if (c == "hello" || c == "status") return cmdStatus(ch);
  if (c == "codes")    return cmdCodes();
  if (c == "clear")    return cmdClear(a);
  if (c == "freeze")   return cmdFreeze();
  if (c == "ident")    return cmdIdent();
  if (c == "sweep")    return cmdSweep(a);
  if (c == "mon")      return cmdMon(a);
  if (c == "peek")     return cmdPeek(a);
  if (c == "kline")    return cmdKline();
  if (c == "ktest")    return cmdKtest();
  if (c == "abs")      return cmdAbs();
  if (c == "tps")      return cmdTps(a);
  if (c == "peak") {
    pkRpm = 0; pkSpd = 0; pkEct = -100;
    return "{\"t\":\"peak\",\"ok\":true,\"msg\":\"Peaks reset\"}";
  }
  if (c == "raw")      return cmdRaw(a);
  if (c == "settings") return cmdSettings();
  if (c == "set")      return cmdSet(a);
  if (c == "log")      return cmdLog(a);
  if (c == "logrows")  return cmdLogRows(a);
  if (c == "live") {
    bool on = a != "off";
    if (ch == CH_SERIAL) serialLive = on;
    else webLiveUntil = millis() + 3000;
    liveWantUntil = millis() + 3000;
    return liveJson();
  }
  if (c == "reboot") {
    rebootAt = millis() + 800;
    return "{\"t\":\"reboot\",\"ok\":true,\"msg\":\"Rebooting...\"}";
  }
  if (c == "factory") {
    if (a != "yes") return errJson("factory", "Send 'factory yes' to reset all settings");
    prefs.begin("obdojo", false);
    prefs.clear();
    prefs.end();
    rebootAt = millis() + 800;
    return "{\"t\":\"factory\",\"ok\":true,\"msg\":\"Settings reset. Rebooting...\"}";
  }
  if (c == "help") {
    return "{\"t\":\"help\",\"ok\":true,\"msg\":\"status live codes clear freeze ident sweep mon peek kline ktest "
           "abs tps peak raw settings set log logrows reboot factory\"}";
  }
  return errJson("error", "Unknown command: " + c);
}

// =====================================================================
//  Bluetooth Classic - ELM327 emulation for Torque Pro / the Teltonika tracker
// =====================================================================
// The bike speaks KDS, not OBD-II, so the emulator answers standard mode 01 PIDs from the
// live values the K-line task keeps fresh, and looks like a CAN ELM327 to the app (that's the
// format Torque and the Teltonika already handle from OBDuke). Kawasaki service codes show up
// in mode 03 as P10xx, e.g. service code 11 (main throttle sensor) = P1011.
// Read-only KDS requests (21 xx, 1A, 13 xx...) pass straight through, so Torque custom PIDs
// can read any register: "2104" returns "61 04 00 C9".
struct ElmState {
  bool echo = true, lf = false, spaces = true, headers = false;
  uint16_t to = 300;           // reply timeout, ms
} elm;
static String elmBuf;
static uint32_t elmWantUntil = 0;

static void elmOut(const String &s) {
  SerialBT.print(s);
  SerialBT.print(elm.lf ? "\r\n" : "\r");
}
static void elmPrompt() { SerialBT.print(elm.lf ? "\r\n>" : "\r>"); }

static String elmHex(const uint8_t *d, int n) {
  String o;
  char b[4];
  for (int i = 0; i < n; i++) {
    snprintf(b, sizeof(b), "%02X", d[i]);
    if (i && elm.spaces) o += ' ';
    o += b;
  }
  return o;
}

static void elmReset() { elm = ElmState(); }

static bool liveVal(int slot, float &v, uint32_t maxAge = 30000) {
  LiveData d = liveSnap();
  if (!(d.ok & (1UL << slot)) || millis() - d.ms > maxAge) return false;
  v = d.v[slot];
  return true;
}

// Wait for the K-line task to fill a value (first request after the key goes on)
static bool waitVal(int slot, float &v) {
  uint32_t t0 = millis(), lim = elm.to < 800 ? elm.to : 800;
  liveWantUntil = millis() + 8000;
  while (!liveVal(slot, v)) {
    if (millis() - t0 >= lim) return false;
#if K_TASK
    delay(10);
#else
    klService();
#endif
  }
  return true;
}

static String elmVolts() {
  float v;
  if (liveVal(L_VBAT, v, 10000)) return fmtF(v, 1) + "V";
  if (readAdcVbat(v)) return fmtF(v, 1) + "V";
  return "12.0V";
}

// Print a reply like a CAN ELM327: single line, or "014 / 0: ... / 1: ..." multi-frame
static void elmReply(const uint8_t *r, int n) {
  String sp = elm.spaces ? " " : "";
  String h = String("7E8") + sp;
  if (n <= 7) {
    String line;
    if (elm.headers) { uint8_t pl = n; line = h + elmHex(&pl, 1) + sp; }
    elmOut(line + elmHex(r, n));
    return;
  }
  if (elm.headers) {
    uint8_t f[8] = {(uint8_t)(0x10 | ((n >> 8) & 0x0F)), (uint8_t)(n & 0xFF)};
    memcpy(f + 2, r, 6);
    elmOut(h + elmHex(f, 8));
    uint8_t seq = 1;
    for (int pos = 6; pos < n; pos += 7, seq++) {
      int k = min(7, n - pos);
      uint8_t c[8];
      c[0] = 0x20 | (seq & 0x0F);
      memcpy(c + 1, r + pos, k);
      elmOut(h + elmHex(c, k + 1));
    }
    return;
  }
  char l[12];
  snprintf(l, sizeof(l), "%03X", (unsigned)(n & 0xFFF));
  elmOut(l);
  elmOut("0:" + sp + elmHex(r, 6));
  int idx = 1;
  for (int pos = 6; pos < n; pos += 7, idx++) {
    char p[4];
    snprintf(p, sizeof(p), "%X:", idx & 0x0F);
    elmOut(String(p) + sp + elmHex(r + pos, min(7, n - pos)));
  }
}

// PIDs the emulator can answer
static const uint8_t ELM_PIDS[] = {0x01, 0x05, 0x0B, 0x0C, 0x0D, 0x0F, 0x11, 0x1C, 0x20, 0x33, 0x40, 0x42};
static void pidBitmap(uint8_t base, uint8_t *d) {
  memset(d, 0, 4);
  for (uint8_t p : ELM_PIDS) {
    if (p <= base || p > base + 32) continue;
    int i = p - base - 1;
    d[i / 8] |= 0x80 >> (i % 8);
  }
}

// One mode 01 PID from the live values. Returns data length (without 41 + PID), or -1.
static int elmPid01(uint8_t pid, uint8_t *d) {
  float v;
  switch (pid) {
    case 0x00: case 0x20: case 0x40: pidBitmap(pid, d); return 4;
    case 0x01: {
      LiveData s = liveSnap();
      int n = s.dtc < 0 ? 0 : (s.dtc > 127 ? 127 : s.dtc);
      d[0] = (s.fi == 1 ? 0x80 : 0) | n; d[1] = 0; d[2] = 0; d[3] = 0;
      return 4;
    }
    case 0x05: if (!waitVal(L_ECT, v)) return -1; d[0] = constrain((int)lroundf(v + 40), 0, 255); return 1;
    case 0x0F: if (!waitVal(L_IAT, v)) return -1; d[0] = constrain((int)lroundf(v + 40), 0, 255); return 1;
    case 0x0B: if (!waitVal(L_MAP, v)) return -1; d[0] = constrain((int)lroundf(v), 0, 255); return 1;
    case 0x33: if (!waitVal(L_BARO, v)) return -1; d[0] = constrain((int)lroundf(v), 0, 255); return 1;
    case 0x0D: if (!waitVal(L_SPD, v)) return -1; d[0] = constrain((int)lroundf(v), 0, 255); return 1;
    case 0x11: if (!waitVal(L_TPS, v)) return -1; d[0] = constrain((int)lroundf(v * 2.55f), 0, 255); return 1;
    case 0x0C: {
      if (!waitVal(L_RPM, v)) return -1;
      uint16_t raw = constrain((int)lroundf(v * 4), 0, 65535);
      d[0] = raw >> 8; d[1] = raw & 0xFF;
      return 2;
    }
    case 0x42: {
      if (!liveVal(L_VBAT, v, 10000) && !readAdcVbat(v) && !waitVal(L_VBAT, v)) return -1;
      uint16_t raw = constrain((int)lroundf(v * 1000), 0, 65535);
      d[0] = raw >> 8; d[1] = raw & 0xFF;
      return 2;
    }
    case 0x1C: d[0] = 0x05; return 1;     // not OBD compliant
  }
  return -1;
}

// Extra Torque PIDs, mode 22 F0xx: 01-04 custom signals x10 (signed), 10 gear,
// 11 RPM, 12 sub-throttle mV, 13 main throttle mV, 14 main throttle raw. Returns reply length or 0.
static int elmVirtual22(const uint8_t *rq, int len, uint8_t *r) {
  if (!sBool(K_TRK_VIRT) || len != 3 || rq[0] != 0x22 || rq[1] != 0xF0) return 0;
  r[0] = 0x62; r[1] = 0xF0; r[2] = rq[2];
  float v;
  auto put16 = [&](int x) { x = constrain(x, -32768, 65535); r[3] = (uint16_t)x >> 8; r[4] = (uint16_t)x & 0xFF; return 5; };
  if (rq[2] >= 0x01 && rq[2] <= 0x04) {
    if (!waitVal(L_C1 + rq[2] - 1, v)) return 0;
    int32_t x = lroundf(v * 10);
    x = constrain(x, -32768L, 32767L);
    return put16((int)x);
  }
  switch (rq[2]) {
    case 0x10: if (!waitVal(L_GEAR, v)) return 0; r[3] = (uint8_t)v; return 4;
    case 0x11: if (!waitVal(L_RPM, v)) return 0; return put16((int)v);
    case 0x12: if (!waitVal(L_SUB, v)) return 0; return put16((int)lroundf(v * 1000));
    case 0x13: if (!waitVal(L_TPSV, v)) return 0; return put16((int)lroundf(v * 1000));
    case 0x14: if (!waitVal(L_TPSV, v)) return 0; return put16((int)tpsRaw);
  }
  return 0;
}

static int elmCodes(uint8_t *r) {
  DtcCache d;
  DLOCK();
  d = dtcCur;
  DUNLOCK();
  if (d.n < 0 || millis() - d.at > 3000) {
    uint8_t rq[2] = {0x13, 0x01}, rr[64];
    int n = ecuReq(rq, 2, rr, sizeof(rr), 600);
    DtcCache nd = {millis(), -1, {0}, {0}};
    nd.n = parseDtc(rr, n, nd.c, nd.st, 8);
    if (nd.n < 0) return 0;
    DLOCK();
    dtcCur = nd;
    live.dtc = nd.n;
    live.fi = nd.n > 0 ? 1 : 0;
    DUNLOCK();
    d = nd;
  }
  r[0] = 0x43;
  r[1] = d.n;
  for (int i = 0; i < d.n && i < 8; i++) { r[2 + i * 2] = 0x10; r[3 + i * 2] = d.c[i]; }
  return 2 + (d.n < 8 ? d.n : 8) * 2;
}

static void elmAt(const String &a) {
  if (a == "Z" || a == "WS")       { elmReset(); elmOut(""); elmOut("ELM327 v1.5"); }
  else if (a == "D")               { bool e = elm.echo; elmReset(); elm.echo = e; elmOut("OK"); }
  else if (a == "I")               elmOut("ELM327 v1.5");
  else if (a == "@1" || a == "@2") elmOut("OBDojo");
  else if (a == "E0" || a == "E1") { elm.echo = a == "E1"; elmOut("OK"); }
  else if (a == "L0" || a == "L1") { elm.lf = a == "L1"; elmOut("OK"); }
  else if (a == "S0" || a == "S1") { elm.spaces = a == "S1"; elmOut("OK"); }
  else if (a == "H0" || a == "H1") { elm.headers = a == "H1"; elmOut("OK"); }
  else if (a == "DP")              elmOut("AUTO, ISO 15765-4 (CAN 11/500)");
  else if (a == "DPN")             elmOut("A6");
  else if (a == "RV")              elmOut(elmVolts());
  else if (a.startsWith("ST") && a.length() == 4) {
    uint32_t t = strtoul(a.substring(2).c_str(), nullptr, 16) * 4;
    elm.to = t ? constrain(t, 40UL, 1000UL) : 300;
    elmOut("OK");
  }
  else if (a == "MA" || a.startsWith("MR") || a.startsWith("MT") || a == "BD" || a.startsWith("PP")) elmOut("?");
  else elmOut("OK");   // SP, SH, AT, CAF, CFC, PC, M0, R1, etc: accepted, nothing to change
}

static void elmHandle(String cmd) {
  cmd.trim();
  String u;
  for (size_t i = 0; i < cmd.length(); i++) if (cmd[i] != ' ') u += (char)toupper(cmd[i]);
  if (elm.echo) elmOut(cmd);
  if (!u.length()) { elmPrompt(); return; }
  elmWantUntil = millis() + 10000;
  liveWantUntil = millis() + 8000;

  if (u.startsWith("AT")) { elmAt(u.substring(2)); elmPrompt(); return; }
  if (u.startsWith("ST")) { elmOut("?"); elmPrompt(); return; }      // STN chip commands

  for (size_t i = 0; i < u.length(); i++) if (!isxdigit(u[i])) { elmOut("?"); elmPrompt(); return; }
  uint8_t rq[16];
  int len = parseHex(u, rq, 15);   // an odd trailing digit (e.g. "010C1" reply count) is ignored
  if (!len) { elmOut("?"); elmPrompt(); return; }

  if (!ecuFound) {      // key just turned on? give the K-line task a moment to connect
    uint32_t t0 = millis();
    while (!ecuFound && millis() - t0 < 1500) {
#if K_TASK
      delay(20);
#else
      klService();
#endif
    }
  }

  uint8_t r[260];
  int n = 0;
  if (!ecuFound) n = 0;
  else if (rq[0] == 0x01 && len >= 2) {
    // mode 01, possibly several PIDs at once (Torque "faster" mode): answer each, combine
    r[0] = 0x41;
    n = 1;
    for (int i = 1; i < len && i <= 6; i++) {
      uint8_t d[4];
      int dl = elmPid01(rq[i], d);
      if (dl < 0) continue;
      r[n++] = rq[i];
      memcpy(r + n, d, dl);
      n += dl;
    }
    if (n == 1) n = 0;
  } else if (rq[0] == 0x03) {
    n = elmCodes(r);
  } else if (rq[0] == 0x07 || rq[0] == 0x0A) {
    r[0] = rq[0] + 0x40; r[1] = 0; n = 2;
  } else if (rq[0] == 0x04) {
    uint8_t cq[2] = {0x14, 0x01}, cr[16];
    int m = ecuReq(cq, 2, cr, sizeof(cr), 3000);
    if (m >= 1 && cr[0] == 0x54) {
      DLOCK();
      live.fi = 0; live.dtc = 0; dtcCur.n = 0; dtcCur.at = millis();
      DUNLOCK();
      r[0] = 0x44; n = 1;
    } else if (m >= 1) { r[0] = 0x7F; r[1] = 0x04; r[2] = 0x22; n = 3; }
  } else if ((n = elmVirtual22(rq, len, r)) > 0) {
  } else if (rq[0] == 0x21 || rq[0] == 0x1A || rq[0] == 0x12 || rq[0] == 0x13 || rq[0] == 0x17 || rq[0] == 0x18) {
    n = ecuReq(rq, len, r, sizeof(r), elm.to < 300 ? 300 : elm.to);
  }

  if (n <= 0) elmOut(!ecuFound && rq[0] == 0x01 && len >= 2 && rq[1] == 0x00 ? "UNABLE TO CONNECT" : "NO DATA");
  else elmReply(r, n);
  elmPrompt();
}

// A line starting with '!' is an OBDojo app command (same commands as the web page /api);
// the reply is one line of JSON. Anything else is ELM327 for Torque / the tracker.
static void btAppLine(const String &line) {
  btAppAt = millis();
  String r = runCommand(line.substring(1), CH_BT);
  SerialBT.print(r);
  SerialBT.print("\r\n");
}

static void trackerTask() {
  if (!trkStarted) return;
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (c == '\r' || c == '\n') {
      if (elmBuf.length()) {
        if (elmBuf[0] == '!') btAppLine(elmBuf);
        else elmHandle(elmBuf);
        elmBuf = "";
      }
    } else if (elmBuf.length() < 200) {
      elmBuf += c;
    }
  }
}

// =====================================================================
//  Web server
// =====================================================================
static bool webAuth() {
  // no login unless both a user and a password are set
  if (!sStr(K_WEB_USER).length() || !sStr(K_WEB_PASS).length()) return true;
  if (web.authenticate(sStr(K_WEB_USER).c_str(), sStr(K_WEB_PASS).c_str())) return true;
  web.requestAuthentication(BASIC_AUTH, "OBDojo");
  return false;
}

static void webStart() {
  web.on("/", HTTP_GET, []() {
    if (!webAuth()) return;
    web.send_P(200, "text/html", WEB_UI);
  });
  web.on("/api", []() {
    if (!webAuth()) return;
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", runCommand(web.arg("c"), CH_WEB));
  });
  web.on("/live", HTTP_GET, []() {
    if (!webAuth()) return;
    webLiveUntil = millis() + 3000;
    liveWantUntil = millis() + 3000;
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", liveJson());
  });
  web.on("/log.csv", HTTP_GET, []() {
    if (!webAuth()) return;
    web.sendHeader("Content-Disposition", "attachment; filename=obdojo_log.csv");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "text/csv", "");
    web.sendContent(logHeader());
    String chunk;
    for (int i = 0; i < logCount; i++) {
      chunk += logRow(logIndex(i));
      if (chunk.length() > 1000) { web.sendContent(chunk); chunk = ""; }
    }
    if (chunk.length()) web.sendContent(chunk);
    web.sendContent("");
  });
  web.on("/sweep.csv", HTTP_GET, []() {
    if (!webAuth()) return;
    web.sendHeader("Content-Disposition", "attachment; filename=obdojo_sweep.csv");
    web.setContentLength(CONTENT_LENGTH_UNKNOWN);
    web.send(200, "text/csv", "");
    web.sendContent("register,name,result,bytes,hex,decimal\n");
    String chunk;
    for (int reg = 0; reg < 256; reg++) {
      SweepE e;
      DLOCK();
      e = sweep[reg];
      DUNLOCK();
      if (!e.st) continue;
      String dec;
      for (int i = 0; i < e.n; i++) { if (i) dec += " "; dec += String(e.d[i]); }
      chunk += hex2(reg) + "," + String(regName(reg)) + "," +
               (e.st == 1 ? "data" : e.st == 2 ? String("refused ") + hex2(e.nrc) : "no reply") + "," +
               String(e.n) + "," + toHex(e.d, e.n) + "," + dec + "\n";
      if (chunk.length() > 1000) { web.sendContent(chunk); chunk = ""; }
    }
    if (chunk.length()) web.sendContent(chunk);
    web.sendContent("");
  });
  web.on("/manifest.json", HTTP_GET, []() {
    web.send(200, "application/manifest+json",
             "{\"name\":\"OBDojo\",\"short_name\":\"OBDojo\",\"start_url\":\"/\",\"display\":\"standalone\","
             "\"background_color\":\"#0a0a0a\",\"theme_color\":\"#6cc24a\","
             "\"icons\":[{\"src\":\"/icon.svg\",\"sizes\":\"any\",\"type\":\"image/svg+xml\"}]}");
  });
  web.on("/icon.svg", HTTP_GET, []() { web.send_P(200, "image/svg+xml", ICON_SVG); });

  // Captive portal probes from Android / iOS / Windows
  auto toRoot = []() {
    web.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    web.send(302, "text/plain", "");
  };
  web.on("/generate_204", toRoot);
  web.on("/gen_204", toRoot);
  web.on("/hotspot-detect.html", toRoot);       // iPhone / Mac
  web.on("/library/test/success.html", toRoot);
  web.on("/canonical.html", toRoot);            // Firefox
  web.on("/success.txt", toRoot);
  web.on("/mobile/status.php", toRoot);         // older Android
  web.on("/redirect", toRoot);
  web.on("/fwlink", toRoot);                    // Windows
  web.on("/connecttest.txt", toRoot);
  web.on("/ncsi.txt", toRoot);
  web.onNotFound(toRoot);

  web.begin();
  webStarted = true;
}

static void wifiStart() {
  int mode = sInt(K_WIFI_MODE);
  if (mode == 2) { WiFi.mode(WIFI_OFF); return; }
  WiFi.setHostname(sStr(K_HOSTNAME).c_str());
  bool sta = mode == 1 && sStr(K_STA_SSID).length();
  WiFi.mode(sta ? WIFI_AP_STA : WIFI_AP);
  const char *pass = sStr(K_AP_PASS).length() >= 8 ? sStr(K_AP_PASS).c_str() : nullptr;
  WiFi.softAP(sStr(K_AP_SSID).c_str(), pass, sInt(K_AP_CH), sBool(K_AP_HIDDEN));
  // Hotspot address (default 1.1.1.1). Phones get 1.1.1.2 onwards; DNS points back at us
  // so the captive portal still opens the page.
  IPAddress ip;
  if (!ip.fromString(sStr(K_AP_IP))) ip = IPAddress(1, 1, 1, 1);
  delay(100);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0), IPAddress((uint32_t)0), ip))
#else
  if (!WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0)))
#endif
    Serial.println("Hotspot address not accepted, using default");
  if (sta) WiFi.begin(sStr(K_STA_SSID).c_str(), sStr(K_STA_PASS).c_str());
  if (sBool(K_CAPTIVE)) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());
    dnsStarted = true;
  }
  MDNS.begin(sStr(K_HOSTNAME).c_str());
  MDNS.addService("http", "tcp", 80);
  webStart();
  Serial.printf("Hotspot %s at %s\n", sStr(K_AP_SSID).c_str(), WiFi.softAPIP().toString().c_str());
}

// =====================================================================
//  Status LED: slow blink = no ECU, solid = ECU ok, fast blink = codes stored,
//  double blink = sweep / monitor running
// =====================================================================
static void ledTask() {
  int pin = sInt(K_LED_PIN);
  if (pin <= 0) return;
  uint32_t t = millis();
  bool on;
  if (!ecuFound) on = (t / 700) % 2;
  else if (kMode != KM_LIVE) on = (t % 1000) < 120 || ((t % 1000) > 240 && (t % 1000) < 360);
  else if (live.fi == 1) on = (t / 150) % 2;
  else on = true;
  digitalWrite(pin, on);
}

// =====================================================================
//  Deep sleep (optional): after sleep_min minutes with no ECU and nobody connected, sleep and
//  wake every sleep_s seconds to check for the ECU. A wake with the key off goes straight back
//  to sleep without starting Wi-Fi or Bluetooth.
// =====================================================================
static uint32_t lastActive = 0;
static void goSleep() {
#if K_TASK
  Serial.printf("No ECU for a while: sleeping, checking every %d s\n", sInt(K_SLEEP_S));
  Serial.flush();
  if (kc.cs > 0) digitalWrite(kc.cs, LOW);      // MCP2003A power-down (its CS pull-down holds it)
  int led = sInt(K_LED_PIN);
  if (led > 0) digitalWrite(led, LOW);
  esp_sleep_enable_timer_wakeup((uint64_t)sInt(K_SLEEP_S) * 1000000ULL);
  esp_deep_sleep_start();
#endif
}

static void sleepTask() {
  int mins = sInt(K_SLEEP_MIN);
  if (ecuFound || (webStarted && WiFi.softAPgetStationNum() > 0) || (trkStarted && SerialBT.hasClient()) || logging)
    lastActive = millis();
  if (mins > 0 && millis() - lastActive > (uint32_t)mins * 60000UL) goSleep();
}

// =====================================================================
//  Setup / loop
// =====================================================================
static String serialBuf;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nOBDojo " FW_VERSION);
  loadSettings();
  for (int i = 0; i < 4; i++) {
    String e = sigParse(sStr(K_SIG1 + i), sigs[i]);
    if (e.length()) Serial.printf("Signal %d ignored: %s\n", i + 1, e.c_str());
  }
  {
    uint8_t tmp[R_N];
    String e = regsParse(sStr(K_REGS), tmp);
    if (e.length()) Serial.printf("Register list ignored (%s), using defaults\n", e.c_str());
  }
  applyKCfg();
  if (sInt(K_LED_PIN) > 0) pinMode(sInt(K_LED_PIN), OUTPUT);
  memset(&ks, 0, sizeof(ks));
  ks.echo = -1;
  klPinsInit();
  Serial.printf("K-line on TX %d / RX %d, CS %d, ECU %02X\n", kc.tx, kc.rx, kc.cs, kc.ecu);

#if K_TASK
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    // woke from sleep: is the bike on? (auto tester mode tries F1 then F2)
    bool on = klOpen(kc.ecu) || (kc.testerSel == 0 && klOpen(kc.ecu));
    if (!on) goSleep();
    onConnected();
  }
  jobDone = xSemaphoreCreateBinary();
#endif

  // Start Wi-Fi FIRST while memory is plentiful - it needs large buffers.
  // Bluetooth Classic (tracker) starts after and uses what is left.
  Serial.printf("Free heap %lu\n", (unsigned long)ESP.getFreeHeap());
  wifiStart();
  Serial.printf("Wi-Fi up. Free heap %lu\n", (unsigned long)ESP.getFreeHeap());

  if (sBool(K_TRK_ON)) {
    // We only use Bluetooth Classic - hand the BLE controller's RAM back to the heap
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    String pin = sStr(K_TRK_PIN);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    SerialBT.setPin(pin.c_str(), pin.length());
#else
    SerialBT.setPin(pin.c_str());
#endif
    trkStarted = SerialBT.begin(sStr(K_BT_NAME));
    Serial.printf("Bluetooth ELM327 %s. Free heap %lu\n", trkStarted ? "ready" : "FAILED", (unsigned long)ESP.getFreeHeap());
  }
  if (sBool(K_LOG_AUTO)) { logging = logAlloc(); logStartMs = millis(); }
  lastActive = millis();

#if K_TASK
  // K-line task: core 1 (with loop), one priority above loop so its timing stays tight
  xTaskCreatePinnedToCore(klTask, "kline", 8192, nullptr, 2, nullptr, 1);
#endif
  Serial.println("Ready. Type 'help'.");
}

void loop() {
  // USB serial commands
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length()) { Serial.println(runCommand(serialBuf, CH_SERIAL)); serialBuf = ""; }
    } else if (serialBuf.length() < 160) serialBuf += c;
  }

  trackerTask();
  if (webStarted) web.handleClient();
  if (dnsStarted) dns.processNextRequest();

  bool want = serialLive || logging || (int32_t)(webLiveUntil - millis()) > 0 ||
              (int32_t)(elmWantUntil - millis()) > 0 || (trkStarted && SerialBT.hasClient());
  if (want) liveWantUntil = millis() + 1500;

#if !K_TASK
  klService();
#endif

  if (serialLive && millis() - lastSerialLive >= (uint32_t)sInt(K_LIVE_MS)) {
    lastSerialLive = millis();
    Serial.println(liveJson());
  }
  if (logging && millis() - lastLogMs >= (uint32_t)sInt(K_LOG_MS)) {
    lastLogMs = millis();
    logSample();
  }
  ledTask();
  sleepTask();

  if (rebootAt && (int32_t)(millis() - rebootAt) > 0) ESP.restart();
  delay(1);
}
