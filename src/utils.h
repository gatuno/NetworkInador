#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdint.h>

uint32_t utils_ip4_netmask_to_prefix (uint32_t netmask);
uint32_t utils_ip4_prefix_to_netmask (uint32_t prefix);

#endif

