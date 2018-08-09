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

#include <sys/stat.h>

#include "manager.h"
#include "interfaces.h"
#include "network-inador.h"

#define SOCKET_PATH "/tmp/network-inador.socket"

enum {
	MANAGER_COMMAND_REQUEST = 0,
	MANAGER_COMMAND_LIST_IFACES = 0,
	
	MANAGER_COMMAND_SET_IPV4,
	
	MANAGER_RESPONSE = 128,
	MANAGER_RESPONSE_REQUEST_INVALID = 128,
	MANAGER_RESPONSE_PROCESING,
	MANAGER_RESPONSE_LIST_IFACES,
	
};

#define MANAGER_IFACE_TYPE_WIRELESS 2
#define MANAGER_IFACE_TYPE_BRIDGE 4
#define MANAGER_IFACE_TYPE_LOOPBACK 8

static void _manager_send_invalid_request (int sock, struct sockaddr_un *client, socklen_t socklen, int seq) {
	unsigned char buffer[128];
	
	buffer[0] = MANAGER_RESPONSE_REQUEST_INVALID;
	buffer[1] = seq;
	
	sendto (sock, buffer, 2, 0, (struct sockaddr *) client, socklen);
}

static void _manager_send_processing (int sock, struct sockaddr_un *client, socklen_t socklen, int seq) {
	unsigned char buffer[128];
	
	buffer[0] = MANAGER_RESPONSE_PROCESING;
	buffer[1] = seq;
	
	sendto (sock, buffer, 2, 0, (struct sockaddr *) client, socklen);
}

static void _manager_send_list_interfaces (NetworkInadorHandle *handle, int sock, struct sockaddr_un *client, socklen_t socklen, int seq) {
	unsigned char buffer[8192];
	Interface *iface_g;
	int pos;
	int flags;
	
	buffer[0] = MANAGER_RESPONSE_LIST_IFACES;
	buffer[1] = seq;
	
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
	
	sendto (sock, buffer, pos, 0, (struct sockaddr *) client, socklen);
}

static void _manager_handle_interface_set_ipv4 (NetworkInadorHandle *handle, char *buffer_read, int len, int sock, struct sockaddr_un *client, socklen_t socklen, int seq) {
	/* Primero, validar que haya suficientes bytes:
	 * 1 byte de la interfaz
	 * 4 bytes de la dirección
	 * 1 byte del prefix
	 */
	uint32_t prefix;
	struct in_addr sin_addr;
	int index;
	Interface *iface;
	IPv4 address;
	
	if (len < 6) {
		/* Bytes unsuficientes */
		_manager_send_invalid_request (sock, client, socklen, seq);
		
		return;
	}
	
	prefix = buffer_read[5];
	
	if (prefix < 0 || prefix > 32) {
		_manager_send_invalid_request (sock, client, socklen, seq);
		return;
	}
	
	index = buffer_read[0];
	
	iface = interfaces_locate_by_index (handle->interfaces, index);
	
	if (iface == NULL) {
		_manager_send_invalid_request (sock, client, socklen, seq);
		return;
	}
	
	memcpy (&address.sin_addr, &buffer_read[1], sizeof (struct in_addr));
	address.prefix = prefix;
	address.next = NULL;
	
	interfaces_clear_all_ipv4_address (handle, iface);
	interfaces_manual_add_ipv4 (handle->netlink_sock_request, iface, &address);
	
	_manager_send_processing (sock, client, socklen, seq);
}

static gboolean _manager_client_data (GIOChannel *source, GIOCondition condition, gpointer data) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) data;
	int sock;
	unsigned char buffer[4096];
	struct sockaddr_un client_name;
	socklen_t socklen;
	int len;
	int seq;
	
	sock = g_io_channel_unix_get_fd (source);
	
	socklen = sizeof (client_name);
	len = recvfrom (sock, buffer, sizeof (buffer), 0, (struct sockaddr *) &client_name, &socklen);
	
	/* Procesar aquí la petición */
	if (len < 2) {
		_manager_send_invalid_request (sock, &client_name, socklen, 0);
		
		return TRUE;
	}
	
	seq = buffer[1];
	
	switch (buffer[0]) {
		case MANAGER_COMMAND_LIST_IFACES:
			_manager_send_list_interfaces (handle, sock, &client_name, socklen, seq);
			break;
		case MANAGER_COMMAND_SET_IPV4:
			_manager_handle_interface_set_ipv4 (handle, &buffer[2], len - 2, sock, &client_name, socklen, seq);
			break;
	}
	
	return TRUE;
}


int manager_setup_socket (NetworkInadorHandle *handle) {
	int sock;
	struct sockaddr_un socket_name;
	GIOChannel *channel;
	
	sock = socket (AF_UNIX, SOCK_DGRAM, 0);
	
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
