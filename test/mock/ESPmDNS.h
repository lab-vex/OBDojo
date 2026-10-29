#pragma once
struct MDNSMock { void begin(const char *) {} void addService(const char *, const char *, int) {} };
extern MDNSMock MDNS;
