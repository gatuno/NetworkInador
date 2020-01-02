/*
 * common.h
 * This file is part of Network-inador
 *
 * Copyright (C) 2019, 2020 - Félix Arreola Rodríguez
 *
 * Network-inador is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Network-inador is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Network-inador; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, 
 * Boston, MA  02110-1301  USA
 */

#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdint.h>

#include <netinet/in.h>
#include <net/ethernet.h>
#include <linux/if.h>

#include <glib.h>
#include <gmodule.h>

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE !FALSE
#endif

typedef struct _IPAddr {
	sa_family_t family;
	union {
		struct in_addr sin_addr;
		struct in6_addr sin6_addr;
	};
	uint32_t prefix;
	
	unsigned char flags;
	unsigned char scope;
} IPAddr;

typedef struct _Interface {
	char name[IFNAMSIZ];
	int ifi_type;
	unsigned char real_hw[ETHER_ADDR_LEN * 2 + 1];
	unsigned int index;
	
	/* Para las interfaces dentro de un bridge */
	unsigned int master_index;
	
	unsigned int mtu;
	
	/* Para las interfaces vlan */
	unsigned int vlan_parent;
	
	/* Banderas estilo ioctl */
	short flags;
	
	char wireless_protocol[IFNAMSIZ];
	
	/* Tipo */
	int is_loopback;
	int is_wireless;
	int is_bridge;
	int is_vlan;
	int is_nlmon;
	int is_dummy;
	
	GList *address;
	
	//DHCPStateInfo dhcp_info;
	
	/* Información wireless */
	//WirelessInfo *wireless;
} Interface;

typedef struct {
	GList *interfaces;
	//Routev4 *rtable_v4;
	
	struct nl_sock * nl_sock_route;
	struct nl_sock * nl_sock_route_events;
	guint route_events_source;
} NetworkInadorHandle;

#endif /* __COMMON_H__ */

