#pragma once
#include "Arduino.h"
#include <map>
enum { HTTP_GET = 1, HTTP_ANY = 0 }; enum { BASIC_AUTH };
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
struct WebServer {
  std::map<std::string, std::function<void()>> routes; std::function<void()> nf;
  std::string curArg, out;
  WebServer(int) {}
  void on(const char *p, std::function<void()> f) { routes[p] = f; }
  void on(const char *p, int, std::function<void()> f) { routes[p] = f; }
  void onNotFound(std::function<void()> f) { nf = f; }
  String arg(const char *) { return String(curArg); }
  void send(int, const char *, const String &b) { out = b.s; }
  void send_P(int, const char *, const char *b) { out = b; }
  void sendHeader(const char *, const String &, bool = false) {}
  void setContentLength(size_t) {}
  void sendContent(const String &b) { out += b.s; }
  void begin() {} void handleClient() {}
  bool authenticate(const char *, const char *) { return true; }
  void requestAuthentication(int, const char *) {}
};
