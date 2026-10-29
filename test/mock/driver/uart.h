#pragma once
#include <cstdint>
typedef int uart_port_t;
enum { UART_NUM_0, UART_NUM_1, UART_NUM_2 };
enum { UART_SIGNAL_INV_DISABLE = 0, UART_SIGNAL_TXD_INV = 1 << 5 };
int sim_uart_inverse(uint32_t mask);
inline int uart_set_line_inverse(uart_port_t, uint32_t mask) { return sim_uart_inverse(mask); }
