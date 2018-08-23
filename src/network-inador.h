/*
 * network-inador.h
 * This file is part of Network-inador
 *
 * Copyright (C) 2011 - Félix Arreola Rodríguez
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

#ifndef __NETWORK_INADOR_H__
#define __NETWORK_INADOR_H__

#include <stdint.h>

#include <netinet/in.h>
#include <linux/if.h>
#include <net/ethernet.h>

#include <unistd.h>

#include <glib.h>

/* Para almacenar la información de DHCP */
enum {
	IFACE_NO_DHCP_RUNNING = 0,
	IFACE_DHCP_CLIENT,
};

enum {
	DHCP_CLIENT_DECONFIG = 1,
	DHCP_CLIENT_LEASEFAIL,
	DHCP_CLIENT_BOUND,
	DHCP_CLIENT_RENEW,
	DHCP_CLIENT_NAK
};

typedef struct _DHCPStateInfo {
	int type;
	
	int read_pipe;
	GPid process_pid;
	
	int client_state;
} DHCPStateInfo;

typedef struct _IPv4 {
	struct in_addr sin_addr;
	uint32_t prefix;
	
	unsigned char flags;
	
	struct _IPv4 *next;
} IPv4;

typedef struct _Interface {
	char name[IFNAMSIZ];
	int ifi_type;
	unsigned char real_hw[ETHER_ADDR_LEN * 2 + 1];
	unsigned int index;
	
	/* Para las interfaces dentro de un bridge */
	unsigned int master_index;
	
	unsigned int mtu;
	
	/* Banderas estilo ioctl */
	short flags;
	
	char wireless_protocol[IFNAMSIZ];
	
	/* Tipo */
	int is_loopback;
	int is_wireless;
	int is_bridge;
	int is_vlan;
	int is_nlmon;
	
	IPv4 *v4_address;
	
	DHCPStateInfo dhcp_info;
	
	struct _Interface *next;
} Interface;

typedef struct _Routev4 {
	struct in_addr dest;
	uint32_t prefix;
	
	struct in_addr gateway;
	unsigned int index;
	
	unsigned char table;
	unsigned char type;
	
	struct _Routev4 *next;
} Routev4;

typedef struct {
	Interface *interfaces;
	Routev4 *rtable_v4;
	
	int netlink_sock_request;
} NetworkInadorHandle;

#endif

