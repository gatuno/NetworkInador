/*
 * manager.c
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

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <linux/if_addr.h>
#include <sys/stat.h>

#include "manager.h"
#include "interfaces.h"
#include "network-inador.h"

#define SOCKET_PATH "/tmp/network-inador.socket"

enum {
	MANAGER_COMMAND_REQUEST = 0,
	MANAGER_COMMAND_LIST_IFACES = 0,
	
	MANAGER_COMMAND_BRING_UP_IFACE,
	MANAGER_COMMAND_BRING_DOWN_IFACE,
	
	MANAGER_COMMAND_CLEAR_IPV4,
	MANAGER_COMMAND_ADD_IPV4,
	MANAGER_COMMAND_REMOVE_IPV4,
	
	MANAGER_COMMAND_LIST_IPV4,
	
	MANAGER_COMMAND_RUN_DHCP_CLIENT,
	
	MANAGER_COMMAND_LIST_ROUTES,
	
	MANAGER_COMMAND_ADD_ROUTE_V4,
	MANAGER_COMMAND_REMOVE_ROUTE_V4,
	
	MANAGER_RESPONSE = 128,
	MANAGER_RESPONSE_REQUEST_INVALID = 128,
	
	MANAGER_RESPONSE_PROCESSING,
	
	MANAGER_RESPONSE_LIST_IFACES,
	
	MANAGER_RESPONSE_LIST_IPV4,
	
	MANAGER_RESPONSE_LIST_ROUTES,
};

enum {
	MANAGER_ERROR_UNKNOWN = 0,
	
	MANAGER_ERROR_INCOMPLETE_REQUEST,
	
	MANAGER_ERROR_UNKNOWN_COMMAND,
	
	MANAGER_ERROR_PREFIX_INVALID,
	MANAGER_ERROR_IFACE_INVALID,
	MANAGER_ERROR_IPV4_INVALID
};

#define MANAGER_IFACE_TYPE_WIRELESS 0x02
#define MANAGER_IFACE_TYPE_BRIDGE 0x04
#define MANAGER_IFACE_TYPE_LOOPBACK 0x8
#define MANAGER_IFACE_TYPE_VLAN 0x10
#define MANAGER_IFACE_TYPE_NLMON 0x20

#define MANAGER_IFACE_IPV4_ADDRESS_SECONDARY 0x01

typedef struct {
	int sock;
	struct sockaddr_un client;
	socklen_t socklen;
	
	int seq;
	
	int command;
	unsigned char *full_buffer;
	unsigned char *command_data;
	
	int full_len;
	int command_len;
	
} ManagerCommandRequest;

static void _manager_send_invalid_request (ManagerCommandRequest *request, int error) {
	unsigned char buffer[128];
	
	buffer[0] = MANAGER_RESPONSE_REQUEST_INVALID;
	buffer[1] = request->seq;
	
	sendto (request->sock, buffer, 2, 0, (struct sockaddr *) &request->client, request->socklen);
}

static void _manager_send_processing (ManagerCommandRequest *request) {
	unsigned char buffer[128];
	
	buffer[0] = MANAGER_RESPONSE_PROCESSING;
	buffer[1] = request->seq;
	
	sendto (request->sock, buffer, 2, 0, (struct sockaddr *) &request->client, request->socklen);
}

static void _manager_send_response (ManagerCommandRequest *request, unsigned char *buffer, int len) {
	sendto (request->sock, buffer, len, 0, (struct sockaddr *) &request->client, request->socklen);
}

static void _manager_send_list_interfaces (NetworkInadorHandle *handle, ManagerCommandRequest *request) {
	unsigned char buffer[8192];
	Interface *iface_g;
	int pos;
	int flags;
	
	buffer[0] = MANAGER_RESPONSE_LIST_IFACES;
	buffer[1] = request->seq;
	
	iface_g = handle->interfaces;
	
	pos = 2;
	while (iface_g != NULL) {
		buffer[pos] = iface_g->index;
		flags = 0;
		
		if (iface_g->is_loopback) {
			flags |= MANAGER_IFACE_TYPE_LOOPBACK;
		}
		
		if (iface_g->is_wireless) {
			flags |= MANAGER_IFACE_TYPE_WIRELESS;
		}
		
		if (iface_g->is_bridge) {
			flags |= MANAGER_IFACE_TYPE_BRIDGE;
		}
		
		if (iface_g->is_vlan) {
			flags |= MANAGER_IFACE_TYPE_VLAN;
		}
		
		if (iface_g->is_nlmon) {
			flags |= MANAGER_IFACE_TYPE_NLMON;
		}
		
		buffer[pos + 1] = flags;
		
		/* Copiar la mac address */
		memcpy (&buffer[pos + 2], iface_g->real_hw, ETHER_ADDR_LEN);
		
		/* Copiar el nombre y el terminador de cadena */
		memcpy (&buffer[pos + 2 + ETHER_ADDR_LEN], iface_g->name, strlen (iface_g->name) + 1);
		
		pos += 2 + ETHER_ADDR_LEN + strlen (iface_g->name) + 1;
		iface_g = iface_g->next;
	}
	
	buffer[pos] = 0;
	pos++;
	
	_manager_send_response (request, buffer, pos);
}

static void _manager_handle_clear_iface (NetworkInadorHandle *handle, ManagerCommandRequest *request) {
	/* Primero, validar que haya suficientes bytes:
	 * 1 byte de la interfaz
	 */
	
	int index;
	Interface *iface;
	
	if (request->command_len < 1) {
		/* Bytes unsuficientes */
		_manager_send_invalid_request (request, MANAGER_ERROR_INCOMPLETE_REQUEST);
		return;
	}
	
	index = request->command_data[0];
	
	iface = interfaces_locate_by_index (handle->interfaces, index);
	
	if (iface == NULL) {
		_manager_send_invalid_request (request, MANAGER_ERROR_IFACE_INVALID);
		return;
	}
	
	interfaces_clear_all_ipv4_address (handle, iface);
	
	_manager_send_processing (request);
}

static void _manager_handle_bring_up_down_iface (NetworkInadorHandle *handle, ManagerCommandRequest *request) {
	/* Primero, validar que haya suficientes bytes:
	 * 1 byte de la interfaz
	 */
	
	int index;
	Interface *iface;
	
	if (request->command_len < 1) {
		/* Bytes unsuficientes */
		_manager_send_invalid_request (request, MANAGER_ERROR_INCOMPLETE_REQUEST);
		return;
	}
	
	index = request->command_data[0];
	
	iface = interfaces_locate_by_index (handle->interfaces, index);
	
	if (iface == NULL) {
		_manager_send_invalid_request (request, MANAGER_ERROR_IFACE_INVALID);
		return;
	}
	
	if (request->command == MANAGER_COMMAND_BRING_UP_IFACE) {
		interfaces_bring_up (handle->netlink_sock_request, iface);
	} else {
		interfaces_bring_down (handle->netlink_sock_request, iface);
	}
	
	_manager_send_processing (request);
}

static void _manager_handle_interface_add_ipv4 (NetworkInadorHandle *handle, ManagerCommandRequest *request) {
	/* Primero, validar que haya suficientes bytes:
	 * 1 byte de la interfaz
	 * 4 bytes de la dirección
	 * 1 byte del prefix
	 */
	uint32_t prefix;
	int index;
	Interface *iface;
	IPv4 address;
	
	if (request->command_len < 6) {
		/* Bytes insuficientes */
		_manager_send_invalid_request (request, MANAGER_ERROR_INCOMPLETE_REQUEST);
		
		return;
	}
	
	prefix = request->command_data[5];
	
	if (prefix < 0 || prefix > 32) {
		_manager_send_invalid_request (request, MANAGER_ERROR_PREFIX_INVALID);
		return;
	}
	
	index = request->command_data[0];
	
	iface = interfaces_locate_by_index (handle->interfaces, index);
	
	if (iface == NULL) {
		_manager_send_invalid_request (request, MANAGER_ERROR_IFACE_INVALID);
		return;
	}
	
	memcpy (&address.sin_addr, &request->command_data[1], sizeof (struct in_addr));
	address.prefix = prefix;
	address.next = NULL;
	
	interfaces_manual_add_ipv4 (handle->netlink_sock_request, iface, &address);
	
	_manager_send_processing (request);
}

static void _manager_handle_interface_del_ipv4 (NetworkInadorHandle *handle, ManagerCommandRequest *request) {
	/* Primero, validar que haya suficientes bytes:
	 * 1 byte de la interfaz
	 * 4 bytes de la dirección
	 * 1 byte del prefix
	 */
	uint32_t prefix;
	struct in_addr sin_addr;
	int index;
	Interface *iface;
	IPv4 *to_del;
	
	if (request->command_len < 6) {
		/* Bytes insuficientes */
		_manager_send_invalid_request (request, MANAGER_ERROR_INCOMPLETE_REQUEST);
		
		return;
	}
	
	prefix = request->command_data[5];
	
	if (prefix < 0 || prefix > 32) {
		_manager_send_invalid_request (request, MANAGER_ERROR_PREFIX_INVALID);
		return;
	}
	
	index = request->command_data[0];
	
	iface = interfaces_locate_by_index (handle->interfaces, index);
	
	if (iface == NULL) {
		_manager_send_invalid_request (request, MANAGER_ERROR_IFACE_INVALID);
		return;
	}
	
	memcpy (&sin_addr, &request->command_data[1], sizeof (struct in_addr));
	
	to_del = _interfaces_serach_ipv4 (iface, sin_addr, prefix);
	
	if (to_del == NULL) {
		_manager_send_invalid_request (request, MANAGER_ERROR_IPV4_INVALID);
		return;
	}
	
	interfaces_manual_del_ipv4 (handle->netlink_sock_request, iface, to_del);
	
	_manager_send_processing (request);
}

static void _manager_send_list_ipv4 (NetworkInadorHandle *handle, ManagerCommandRequest *request) {
	/* Primero, validar que haya suficientes bytes:
	 * 1 byte de la interfaz
	 */
	
	int index;
	Interface *iface;
	unsigned char buffer[8192];
	IPv4 *ip_g;
	int pos;
	int count;
	
	if (request->command_len < 1) {
		/* Bytes unsuficientes */
		_manager_send_invalid_request (request, MANAGER_ERROR_INCOMPLETE_REQUEST);
		return;
	}
	
	index = request->command_data[0];
	
	iface = interfaces_locate_by_index (handle->interfaces, index);
	
	if (iface == NULL) {
		_manager_send_invalid_request (request, MANAGER_ERROR_IFACE_INVALID);
		return;
	}
	
	buffer[0] = MANAGER_RESPONSE_LIST_IPV4;
	buffer[1] = request->seq;
	
	ip_g = iface->v4_address;
	
	count = 0;
	pos = 6;
	
	while (ip_g != NULL) {
		memcpy (&buffer[pos], &ip_g->sin_addr, sizeof (ip_g->sin_addr));
		pos = pos + sizeof (ip_g->sin_addr);
		
		buffer[pos] = ip_g->prefix;
		
		buffer[pos + 1] = 0;
		
		if (ip_g->flags & IFA_F_SECONDARY) {
			buffer[pos + 1] |= MANAGER_IFACE_IPV4_ADDRESS_SECONDARY;
		}
		
		pos = pos + 2;
		
		ip_g = ip_g->next;
		count++;
	}
	
	memcpy (&buffer[2], &count, sizeof (int));
	
	_manager_send_response (request, buffer, pos);
}

static void _manager_send_list_routes (NetworkInadorHandle *handle, ManagerCommandRequest *request) {
	unsigned char buffer[8192];
	Routev4 *route_g;
	int pos;
	int flags;
	unsigned int count;
	
	buffer[0] = MANAGER_RESPONSE_LIST_ROUTES;
	buffer[1] = request->seq;
	
	route_g = handle->rtable_v4;
	
	count = 0;
	pos = 6;
	while (route_g != NULL) {
		memcpy (&buffer[pos], &route_g->dest, sizeof (route_g->dest));
		pos = pos + sizeof (route_g->dest);
		
		buffer[pos] = route_g->prefix;
		buffer[pos + 1] = route_g->index;
		buffer[pos + 2] = route_g->table;
		buffer[pos + 3] = route_g->type;
		pos = pos + 4;
		
		memcpy (&buffer[pos], &route_g->gateway, sizeof (route_g->gateway));
		
		pos = pos + 4;
		
		route_g = route_g->next;
		count++;
	}
	
	memcpy (&buffer[2], &count, sizeof (int));
	
	_manager_send_response (request, buffer, pos);
}

static gboolean _manager_client_data (GIOChannel *source, GIOCondition condition, gpointer data) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) data;
	unsigned char buffer[4096];
	ManagerCommandRequest request;
	
	request.full_buffer = buffer;
	
	request.sock = g_io_channel_unix_get_fd (source);
	
	request.socklen = sizeof (request.client);
	request.full_len = recvfrom (request.sock, buffer, sizeof (buffer), 0, (struct sockaddr *) &request.client, &request.socklen);
	
	request.command_data = &buffer[2];
	request.command_len = request.full_len - 2;
	
	request.seq = 0;
	
	/* Procesar aquí la petición */
	if (request.full_len < 2) {
		_manager_send_invalid_request (&request, MANAGER_ERROR_INCOMPLETE_REQUEST);
		
		return TRUE;
	}
	
	request.command = buffer[0];
	request.seq = buffer[1];
	
	switch (request.command) {
		case MANAGER_COMMAND_LIST_IFACES:
			_manager_send_list_interfaces (handle, &request);
			break;
		case MANAGER_COMMAND_BRING_UP_IFACE:
		case MANAGER_COMMAND_BRING_DOWN_IFACE:
			_manager_handle_bring_up_down_iface (handle, &request);
			break;
		case MANAGER_COMMAND_CLEAR_IPV4:
			_manager_handle_clear_iface (handle, &request);
			break;
		case MANAGER_COMMAND_ADD_IPV4:
			_manager_handle_interface_add_ipv4 (handle, &request);
			break;
		case MANAGER_COMMAND_REMOVE_IPV4:
			_manager_handle_interface_del_ipv4 (handle, &request);
			break;
		case MANAGER_COMMAND_LIST_IPV4:
			_manager_send_list_ipv4 (handle, &request);
			break;
		case MANAGER_COMMAND_LIST_ROUTES:
			_manager_send_list_routes (handle, &request);
			break;
		default:
			_manager_send_invalid_request (&request, MANAGER_ERROR_UNKNOWN_COMMAND);
	}
	
	return TRUE;
}


int manager_setup_socket (NetworkInadorHandle *handle) {
	int sock;
	struct sockaddr_un socket_name;
	GIOChannel *channel;
	
	sock = socket (AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	
	if (sock < 0) {
		perror ("Failed to create AF_UNIX socket");
		
		return -1;
	}
	
	memset (&socket_name, 0, sizeof (struct sockaddr_un));
	
	socket_name.sun_family = AF_UNIX;
	strncpy (socket_name.sun_path, SOCKET_PATH, sizeof (socket_name.sun_path) - 1);
	
	unlink (SOCKET_PATH);
	
	if (bind (sock, (struct sockaddr *) &socket_name, sizeof (struct sockaddr_un)) < 0) {
		perror ("Bind");
		
		return -1;
	}
	
	/* TODO: Aplicar permisos aquí */
	chmod (SOCKET_PATH, 0666);
	
	channel = g_io_channel_unix_new (sock);
	
	g_io_add_watch (channel, G_IO_IN | G_IO_PRI, _manager_client_data, handle);
	
	return 0;
}
