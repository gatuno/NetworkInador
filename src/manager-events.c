/*
 * manager-events.c
 * This file is part of Network-inador
 *
 * Copyright (C) 2018 - Félix Arreola Rodríguez
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/if_addr.h>

#include <linux/wireless.h>

#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>

#include "network-inador.h"
#include "manager-events.h"

#define EVENTS_SOCKET_PATH "/tmp/network-inador.events"
#define MANAGER_EVENTS_MAX_CLIENT 50

static int _manager_events_clients[MANAGER_EVENTS_MAX_CLIENT];
static int _manager_events_client_count = 0;

static gboolean _manager_events_handle_read (GIOChannel *source, GIOCondition condition, gpointer data) {
	char buffer[128];
	
	int sock;
	int ret;
	int g;
	
	sock = g_io_channel_unix_get_fd (source);
	
	ret = read (sock, buffer, sizeof (buffer));
	
	if (ret == 0) {
		/* El socket del cliente se cerró */
		for (g = 0; g < _manager_events_client_count; g++) {
			if (_manager_events_clients[g] == sock) {
				/* Te encontré */
				if (_manager_events_client_count - 1 == g) {
					/* Es el último socket del arreglo */
					_manager_events_clients[g] = 0;
					_manager_events_client_count--;
				} else {
					/* Recorrer el último en posición de éste */
					_manager_events_clients[g] = _manager_events_clients[_manager_events_client_count - 1];
					_manager_events_clients[_manager_events_client_count - 1] = 0;
					_manager_events_client_count--;
				}
				break;
			}
		}
		
		close (sock);
	} else {
		/* Procesar data o errores de lectura */
	}
}

static gboolean _manager_events_handle_new_conn (GIOChannel *source, GIOCondition condition, gpointer data) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) data;
	int sock;
	int new_c;
	GIOChannel *channel;
	
	sock = g_io_channel_unix_get_fd (source);
	
	new_c = accept (sock, NULL, NULL);
	if (new_c < 0) {
		printf ("Error al aceptar cliente\n");
		return TRUE;
	}
	
	if (fcntl (new_c, F_SETFD, FD_CLOEXEC) < 0) {
		printf ("Error set close-on-exec");
	}
	
	if (_manager_events_client_count == MANAGER_EVENTS_MAX_CLIENT) {
		/* Rechazar el cliente por estar al máximo de capacidad */
		close (new_c);
		
		return TRUE;
	}
	
	channel = g_io_channel_unix_new (new_c);
	
	g_io_add_watch (channel, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, _manager_events_handle_read, handle);
	
	/* Agregar a la lista de clientes */
	_manager_events_clients [_manager_events_client_count] = new_c;
	_manager_events_client_count++;
	
	return TRUE;
}

void manager_events_notify_ipv4_address_added (Interface *iface, IPv4 *address) {
	char buffer[128];
	int size;
	int g;
	
	buffer[0] = 0;
	buffer[1] = MANAGER_EVENT_IPV4_ADDED;
	
	buffer[2] = iface->index;
	memcpy (&buffer[3], &address->sin_addr, sizeof (address->sin_addr));
	buffer[7] = address->prefix;
	
	size = 8;
	
	for (g = 0; g < _manager_events_client_count; g++) {
		write (_manager_events_clients [g], buffer, size);
	}
}

void manager_events_setup (NetworkInadorHandle *handle) {
	int sock;
	struct sockaddr_un socket_name;
	GIOChannel *channel;
	
	sock = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	
	if (sock < 0) {
		perror ("Failed to create AF_UNIX socket");
		
		return;
	}
	
	memset (_manager_events_clients, 0, sizeof (_manager_events_clients));
	memset (&socket_name, 0, sizeof (struct sockaddr_un));
	
	socket_name.sun_family = AF_UNIX;
	strncpy (socket_name.sun_path, EVENTS_SOCKET_PATH, sizeof (socket_name.sun_path) - 1);
	
	unlink (EVENTS_SOCKET_PATH);
	
	if (bind (sock, (struct sockaddr *) &socket_name, sizeof (struct sockaddr_un)) < 0) {
		perror ("Bind");
		
		close (sock);
		return;
	}
	
	if (listen (sock, 20)) {
		perror ("Listen");
		
		close (sock);
		unlink (EVENTS_SOCKET_PATH);
		
		return;
	}
	
	/* TODO: Aplicar permisos aquí */
	chmod (EVENTS_SOCKET_PATH, 0666);
	
	channel = g_io_channel_unix_new (sock);
	
	g_io_add_watch (channel, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, _manager_events_handle_new_conn, handle);
}

