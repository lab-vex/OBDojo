#pragma once
typedef int gpio_num_t;
int sim_bus_level();
inline int gpio_get_level(gpio_num_t) { return sim_bus_level(); }
