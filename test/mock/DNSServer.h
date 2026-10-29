#pragma once
#include "Arduino.h"
enum class DNSReplyCode { NoError };
struct DNSServer { void setErrorReplyCode(DNSReplyCode) {} void start(int, const char *, IPAddress) {} void processNextRequest() {} };
