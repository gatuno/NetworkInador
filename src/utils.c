#include <stdint.h>
#include <arpa/inet.h>

uint32_t utils_ip4_netmask_to_prefix (uint32_t netmask) {
	uint32_t prefix;
	uint8_t v;
	const uint8_t *p = (uint8_t *) &netmask;

	if (p[3]) {
		prefix = 24;
		v = p[3];
	} else if (p[2]) {
		prefix = 16;
		v = p[2];
	} else if (p[1]) {
		prefix = 8;
		v = p[1];
	} else {
		prefix = 0;
		v = p[0];
	}

	while (v) {
		prefix++;
		v <<= 1;
	}

	return prefix;
}

/**
 * nm_utils_ip4_prefix_to_netmask:
 * @prefix: a CIDR prefix
 *
 * Returns: the netmask represented by the prefix, in network byte order
 **/
uint32_t utils_ip4_prefix_to_netmask (uint32_t prefix) {
	return prefix < 32 ? ~htonl(0xFFFFFFFF >> prefix) : 0xFFFFFFFF;
}
