/*
 * dhcp.c
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <signal.h>

#include <glib.h>

#include "network-inador.h"
#include "dhcp.h"
#include "interfaces.h"
#include "utils.h"
#include "routes.h"

#define DHCPC_PIPEOUT_HAS_IP 0x01
#define DHCPC_PIPEOUT_HAS_SERVER_IP 0x02
#define DHCPC_PIPEOUT_HAS_OPTS 0x04

static void _dhcp_parse_client_packet (NetworkInadorHandle *handle, Interface *iface, unsigned char *buffer, int len) {
	int type, flags, current_opt, count_opt;
	IPv4 address;
	struct in_addr mask, siaddr_nip, route;
	type = buffer[0];
	int pos;
	int has_gateway = 0;
	
	memset (&address, 0, sizeof (address));
	
	/* Máscara por default */
	inet_pton (AF_INET, "255.255.255.0", &mask);
	
	iface->dhcp_info.client_state = type;
	switch (type) {
		case DHCP_CLIENT_DECONFIG:
			/* Desconfigurar la interfaz, borrar las IPs */
			interfaces_clear_all_ipv4_address (handle, iface);
			interfaces_bring_up (handle->netlink_sock_request, iface);
			break;
		case DHCP_CLIENT_LEASEFAIL:
		case DHCP_CLIENT_NAK:
			/* leasefail, nak */
			break;
		case DHCP_CLIENT_BOUND:
		case DHCP_CLIENT_RENEW:
			/* Bound, renew */
			flags = buffer[1];
			
			pos = 2;
			if (flags & DHCPC_PIPEOUT_HAS_IP) {
				memcpy (&address.sin_addr, &buffer[pos], 4);
				pos += 4;
			}
			
			if (flags & DHCPC_PIPEOUT_HAS_SERVER_IP) {
				memcpy (&siaddr_nip, &buffer[pos], 4);
				pos += 4;
			}
			
			if (flags & DHCPC_PIPEOUT_HAS_OPTS) {
				current_opt = buffer[pos];
				
				while (current_opt != 255) {
					pos++;
					
					if (current_opt == 0x01) {
						memcpy (&mask, &buffer[pos], 4);
						pos += 4;
					} else if (current_opt == 0x03) {
						count_opt = buffer[pos];
						pos++;
						
						/* TODO: Solo tomamos la ultima ruta, arreglar */
						while (count_opt) {
							memcpy (&route, &buffer[pos], 4);
							pos += 4;
							has_gateway = 1;
							
							count_opt--;
						}
					} else if (current_opt == 0x06) {
						/* TODO: Procesar la lista de DNS */
						count_opt = buffer[pos];
						pos++;
						
						while (count_opt) {
							count_opt--;
							pos += 4;
						}
					} else if (current_opt == 0x0c || current_opt == 0x0f) {
						/* TODO: Procesar el hostname o domain name */
						while (buffer[pos] != 0) {
							pos++;
						}
					} else if (current_opt == 0x2a) {
						/* TODO: Procesar la lista de NTP */
						count_opt = buffer[pos];
						pos++;
						
						while (count_opt) {
							count_opt--;
							pos += 4;
						}
					}
					
					current_opt = buffer[pos];
				}
				
				/* Ejecutar la configuración de la IP */
				address.prefix = utils_ip4_netmask_to_prefix (mask.s_addr);
				interfaces_manual_add_ipv4 (handle->netlink_sock_request, iface, &address);
				
				/* Y esperar a que se active la IP para luego configurar la ruta */
				if (has_gateway) {
					IPv4 default_dest;
					
					inet_pton (AF_INET, "0.0.0.0", &default_dest.sin_addr);
					default_dest.prefix = 0;
					routes_manual_add_ipv4 (handle->netlink_sock_request, iface, &default_dest, route);
				}
			}
			break;
	}
}

gboolean _dhcp_read_client_data (GIOChannel *source, GIOCondition condition, gpointer data) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) data;
	int sock;
	char buffer[256];
	int len;
	Interface *iface;
	int exit_estatus;
	int ret;
	
	sock = g_io_channel_unix_get_fd (source);
	
	len = read (sock, buffer, sizeof (buffer));
	
	printf ("\n-------------> Read DHCP client from PIPE (%i) = %i\n", sock, len);
	
	iface = handle->interfaces;
	while (iface != NULL) {
		if (sock == iface->dhcp_info.read_pipe) {
			/* Esta es la interfaz activa */
			break;
		}
		iface = iface->next;
	}
	
	if (iface == NULL) {
		/* Que raro... no debería ocurrir */
		
		return FALSE;
	}
	
	if (len == 0) {
		close (sock);
		
		/* El proceso del cliente de dhcp murió, si lo reiniciamos se maneja en g_child_wait */
		iface->dhcp_info.read_pipe = 0;
		
		return FALSE;
	} else if (len < 0) {
		printf ("_--------------_____________ Soy el error que buscas\n");
		return FALSE;
	} else {
		/* Parsear lo leido por el dhcp client */
		_dhcp_parse_client_packet (handle, iface, buffer, len);
	}
	
	return TRUE;
}

static void _dhcp_client_exited (GPid pid, gint status, gpointer data) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) data;
	Interface *iface;
	
	iface = handle->interfaces;
	while (iface != NULL) {
		if (pid == iface->dhcp_info.process_pid) {
			/* Esta es la interfaz activa */
			break;
		}
		iface = iface->next;
	}
	
	if (iface == NULL) {
		/* El dhcp para una interfaz eliminada murió */
		
		g_spawn_close_pid (pid);
		return;
	}
	
	iface->dhcp_info.process_pid = 0;
	
	/* Si el estado quedó en "running", reiniciar el proceso de dhcp */
	if (iface->dhcp_info.type == IFACE_DHCP_CLIENT) {
		iface->dhcp_info.type = IFACE_NO_DHCP_RUNNING;
		dhcp_run_client (handle, iface);
	}
	
	g_spawn_close_pid (pid);
}

void dhcp_run_client (NetworkInadorHandle *handle, Interface *iface) {
	GIOChannel *channel;
	gboolean result;
	gint read_from_child;
	GPid child_pid;
	
	/* FIXME: Arreglar el path */
	char *argv[] = {"/tmp/abc/sbin/nidhcpc", iface->name, NULL};
	
	if (iface->dhcp_info.type != IFACE_NO_DHCP_RUNNING) {
		printf ("DHCP (client or server) already running\n");
		return;
	}
	
	interfaces_bring_up (handle->netlink_sock_request, iface);
	
	result = g_spawn_async_with_pipes (
		NULL, /* working directory */
		argv, /* Argumentos */
		NULL, /* envp */
		G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_CLOEXEC_PIPES, /* Banderas */
		NULL, NULL, /* Child setup func and data */
		&child_pid,
		NULL, /* Entrada estándar */
		&read_from_child, /* Salida estándar */
		NULL, /* Salida de errores */
		NULL); /* Gerror */
	
	if (result == FALSE) {
		printf ("Falló al lanzar proceso de cliente de dhcp\n");
		return;
	}
	
	iface->dhcp_info.type = IFACE_DHCP_CLIENT;
	iface->dhcp_info.read_pipe = read_from_child;
	iface->dhcp_info.process_pid = child_pid;
	
	iface->dhcp_info.client_state = DHCP_CLIENT_DECONFIG;
	
	/* Instalar un GIOChannel */
	channel = g_io_channel_unix_new (read_from_child);
	
	g_io_add_watch (channel, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, _dhcp_read_client_data, handle);
	
	/* Para manejar la muerte del proceso */
	g_child_watch_add (child_pid, _dhcp_client_exited, handle);
}

void dhcp_stop_client (NetworkInadorHandle *handle, Interface *iface) {
	if (iface->dhcp_info.type == IFACE_DHCP_CLIENT) {
		iface->dhcp_info.type = IFACE_NO_DHCP_RUNNING;
		kill (iface->dhcp_info.process_pid, SIGTERM);
	}
}

