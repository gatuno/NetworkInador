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

typedef struct _IPv4 {
	struct in_addr sin_addr;
	uint32_t prefix;
	
	struct _IPv4 *next;
} IPv4;

typedef struct _Interface {
	char name[IFNAMSIZ];
	int ifi_type;
	char real_hw[ETHER_ADDR_LEN * 2 + 1];
	unsigned int index;
	
	short flags;
	
	char wireless_protocol[IFNAMSIZ];
	
	int is_loopback;
	int is_wireless;
	int is_bridge;
	int is_vlan;
	
	IPv4 *v4_address;
	
	struct _Interface *next;
} Interface;

typedef struct {
	Interface *interfaces;
} NetworkInadorHandle;

#endif

