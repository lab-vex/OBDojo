#pragma once
#include "Arduino.h"
#include <map>
extern std::map<std::string, std::string> g_nvs;
struct Preferences {
  void begin(const char *, bool) {} void end() {}
  String getString(const char *k, const char *d) { auto it = g_nvs.find(k); return it == g_nvs.end() ? String(d) : String(it->second); }
  void putString(const char *k, const String &v) { g_nvs[k] = v.s; }
  void clear() { g_nvs.clear(); }
};
