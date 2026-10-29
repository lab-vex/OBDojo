#pragma once
#include <cstdint>
enum { ESP_MAC_BT };
inline void esp_read_mac(uint8_t *m, int) { for (int i = 0; i < 6; i++) m[i] = i; }
