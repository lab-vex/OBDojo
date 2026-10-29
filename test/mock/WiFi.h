#pragma once
#include "Arduino.h"
enum { WIFI_OFF, WIFI_AP, WIFI_AP_STA, WL_CONNECTED = 3 };
struct WiFiMock {
  IPAddress ap = IPAddress(192, 168, 4, 1);
  void mode(int) {} void setHostname(const char *) {}
  bool softAP(const char *, const char *, int, bool) { return true; }
  bool softAPConfig(IPAddress i, IPAddress, IPAddress, IPAddress = IPAddress(), IPAddress = IPAddress()) { ap = i; return true; }
  IPAddress softAPIP() { return ap; }
  void begin(const char *, const char *) {}
  int status() { return 0; }
  IPAddress localIP() { return IPAddress(); }
  int softAPgetStationNum() { return 1; }
};
extern WiFiMock WiFi;
