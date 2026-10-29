#pragma once
// Just enough of the Arduino-ESP32 API to compile the firmware on a PC.
// Time is simulated: g_us is the clock, delay() moves it on and lets the simulated bike react.
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <cctype>
#include <cstdarg>
#include <algorithm>
#include <functional>
#include <deque>
#define PROGMEM
using std::min; using std::max;
template<class T, class L, class H> auto constrain(T x, L lo, H hi) -> T { return x < (T)lo ? (T)lo : (x > (T)hi ? (T)hi : x); }
extern uint64_t g_us;
void sim_advance_us(uint64_t us);
inline uint32_t millis() { return (uint32_t)(g_us / 1000); }
inline uint32_t micros() { sim_advance_us(5); return (uint32_t)g_us; }   // busy-wait loops still move time
inline void delay(uint32_t ms) { sim_advance_us((uint64_t)ms * 1000); }
inline void delayMicroseconds(uint32_t us) { sim_advance_us(us); }
#define OUTPUT 1
#define INPUT 0
#define LOW 0
#define HIGH 1
void sim_pin_write(int pin, int v);
inline void pinMode(int, int) {}
inline void digitalWrite(int p, int v) { sim_pin_write(p, v); }
inline uint32_t analogReadMilliVolts(int) { return 2100; }

class String {
 public:
  std::string s;
  String() {}
  String(const char *c) : s(c ? c : "") {}
  String(const std::string &x) : s(x) {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(float v, unsigned dp = 2) { char b[40]; snprintf(b, sizeof b, "%.*f", dp, v); s = b; }
  String(double v, unsigned dp = 2) { char b[40]; snprintf(b, sizeof b, "%.*f", dp, v); s = b; }
  size_t length() const { return s.size(); }
  const char *c_str() const { return s.c_str(); }
  char operator[](size_t i) const { return i < s.size() ? s[i] : 0; }
  char &operator[](size_t i) { return s[i]; }
  String &operator+=(const String &o) { s += o.s; return *this; }
  String &operator+=(const char *o) { s += o; return *this; }
  String &operator+=(char c) { s += c; return *this; }
  bool operator==(const String &o) const { return s == o.s; }
  bool operator==(const char *o) const { return s == o; }
  bool operator!=(const String &o) const { return s != o.s; }
  bool operator!=(const char *o) const { return s != o; }
  int indexOf(char c) const { auto p = s.find(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(size_t a) const { return a >= s.size() ? String() : String(s.substr(a)); }
  String substring(size_t a, size_t b) const { if (a >= s.size()) return String(); return String(s.substr(a, b > a ? b - a : 0)); }
  void trim() { size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b - a + 1); }
  long toInt() const { return atol(s.c_str()); }
  float toFloat() const { return atof(s.c_str()); }
  bool startsWith(const char *p) const { return s.rfind(p, 0) == 0; }
  bool startsWith(const String &p) const { return s.rfind(p.s, 0) == 0; }
  void toUpperCase() { for (auto &c : s) c = toupper(c); }
  void toLowerCase() { for (auto &c : s) c = tolower(c); }
  void replace(const char *a, const char *b) { std::string A(a), B(b); size_t p = 0; while ((p = s.find(A, p)) != std::string::npos) { s.replace(p, A.size(), B); p += B.size(); } }
  void reserve(size_t n) { s.reserve(n); }
};
inline String operator+(const String &a, const String &b) { return String(a.s + b.s); }
inline String operator+(const String &a, const char *b) { return String(a.s + b); }
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }
inline String operator+(const String &a, char b) { return String(a.s + b); }
inline String operator+(const String &a, int b) { return a + String(b); }

struct SerialMock {
  std::string in; size_t pos = 0;
  int available() { return pos < in.size(); }
  int read() { return pos < in.size() ? in[pos++] : -1; }
  void begin(int) {}
  void flush() {}
  void print(const String &x) { fputs(x.c_str(), stdout); }
  void print(const char *x) { fputs(x, stdout); }
  void println(const String &x = String()) { puts(x.c_str()); }
  void println(const char *x) { puts(x); }
  int printf(const char *f, ...) { va_list a; va_start(a, f); int r = vprintf(f, a); va_end(a); return r; }
};
extern SerialMock Serial;

// UART2 wired to the simulated K-line (see sim.cpp)
#define SERIAL_8N1 0x800001c
void sim_kl_write(uint8_t b);
void sim_kl_flush();
int sim_kl_available();
int sim_kl_read();
void sim_kl_begin(unsigned long baud, int rx, int tx);
struct HardwareSerial {
  int num;
  HardwareSerial(int n) : num(n) {}
  size_t setRxBufferSize(size_t n) { return n; }
  void begin(unsigned long baud, uint32_t, int rx, int tx) { sim_kl_begin(baud, rx, tx); }
  void end() {}
  int available() { return sim_kl_available(); }
  int read() { return sim_kl_read(); }
  size_t write(uint8_t b) { sim_kl_write(b); return 1; }
  void flush() { sim_kl_flush(); }
};
extern HardwareSerial Serial2;

struct EspMock { uint32_t getFreeHeap() { return 123456; } void restart() { puts("[restart]"); } };
extern EspMock ESP;
class IPAddress {
 public:
  uint8_t b[4] = {0, 0, 0, 0};
  IPAddress() {}
  IPAddress(uint32_t v) { memcpy(b, &v, 4); }
  IPAddress(int a, int c, int d, int e) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
  bool fromString(const String &x) { int a, c, d, e; char t; if (sscanf(x.c_str(), "%d.%d.%d.%d%c", &a, &c, &d, &e, &t) != 4) return false; if (a > 255 || c > 255 || d > 255 || e > 255 || a < 0 || c < 0 || d < 0 || e < 0) return false; b[0] = a; b[1] = c; b[2] = d; b[3] = e; return true; }
  uint8_t operator[](int i) const { return b[i]; }
  String toString() const { char s[20]; snprintf(s, 20, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]); return s; }
};
