#pragma once
#include "Arduino.h"
struct BluetoothSerial {
  std::string in, out; size_t pos = 0; bool client = true;
  void setPin(const char *, int) {}
  bool begin(const String &) { return true; }
  bool hasClient() { return client; }
  int available() { return pos < in.size(); }
  int read() { return pos < in.size() ? in[pos++] : -1; }
  void print(const String &s) { out += s.s; }
  void print(const char *s) { out += s; }
};
