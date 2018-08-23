/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include <stdint.h>

#ifndef UDHCP_DHCPC_H
#define UDHCP_DHCPC_H 1

int udhcpc_main(int pipefd, char *interface);

struct client_config_t {
	uint8_t client_mac[6];          /* Our mac address */
	int ifindex;                    /* Index number of the interface to use */
	uint8_t opt_mask[256 / 8];      /* Bitmask of options to send (-O option) */
	const char *interface;          /* The name of the interface to use */
	char *pidfile;                  /* Optionally store the process ID */
	const char *script;             /* User script to run at dhcp events */
	struct option_set *options;     /* list of DHCP options to send to server */
	uint8_t *clientid;              /* Optional client id to use */
	uint8_t *vendorclass;           /* Optional vendor class-id to use */
	uint8_t *hostname;              /* Optional hostname to use */
	uint8_t *fqdn;                  /* Optional fully qualified domain name to use */

	uint16_t first_secs;
	uint16_t last_secs;
} FIX_ALIASING;

/* server_config sits in 1st half of bb_common_bufsiz1 */
//#define client_config (*(struct client_config_t*)(&bb_common_bufsiz1[COMMON_BUFSIZE / 2]))
extern struct client_config_t client_config;

#define CLIENT_PORT  68
#define CLIENT_PORT6 546

#define SERVER_PORT  67
#define SERVER_PORT6 547

#endif
