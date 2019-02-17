/*
 * network-inador.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <stdint.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>

#include <linux/netlink.h>
#include <linux/if.h>

#include <net/ethernet.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>

#include "network-inador.h"
#include "interfaces.h"
#include "netlink-events.h"
#include "manager.h"
#include "bridge.h"
#include "routes.h"
#include "wireless.h"
#include "manager-events.h"
#include "dhcp.h"

static GMainLoop *loop = NULL;

int create_route_netlink_socket (int groups) {
	int fd;
	struct sockaddr_nl local;
	
	fd = socket (AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	
	if (fd < 0) {
		return -1;
	}
	
	memset(&local, 0, sizeof(local)); /* fill-in local address information */
	local.nl_family = AF_NETLINK;
	local.nl_pid = 0;
	local.nl_groups = groups;
	
	if (bind (fd, (struct sockaddr *) &local, sizeof(local)) < 0) {
		perror("cannot bind, are you root ? if yes, check netlink/rtnetlink kernel support");
		close (fd);
		return -1;
	}
	
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (fd, (struct sockaddr *) &local_nl, &local_size);
	
	printf ("Primer puerto es %ld\n", local_nl.nl_pid);
	
	return fd;
}

static int sigterm_pipe_fds[2] = { -1, -1 };

static void sigterm_handler (int signum) {
	//fprintf (stderr, "SIGTERM SIGINT Handler\n");
	if (sigterm_pipe_fds[1] >= 0) {
		if (write (sigterm_pipe_fds[1], "", 1) == -1 ) {
			//fprintf (stderr, "Write to sigterm_pipe failed.\n");
		}
		close (sigterm_pipe_fds[1]);
		sigterm_pipe_fds[1] = -1;
	}
}

static gboolean quit_handler (GIOChannel *source, GIOCondition cond, gpointer data) {
	g_main_loop_quit (loop);
}

static void setup_signal (void) {
	struct sigaction act;
	sigset_t empty_mask;
	
	/* Preparar el pipe para la señal de cierre */
	if (pipe (sigterm_pipe_fds) != 0) {
		perror ("Failed to create SIGTERM pipe");
		sigterm_pipe_fds[0] = -1;
	}
	
	/* Instalar un manejador de señales para SIGTERM */
	sigemptyset (&empty_mask);
	act.sa_mask    = empty_mask;
	act.sa_flags   = 0;
	act.sa_handler = &sigterm_handler;
	if (sigaction (SIGTERM, &act, NULL) < 0) {
		perror ("Failed to register SIGTERM handler");
	}
	
	if (sigaction (SIGINT, &act, NULL) < 0) {
		perror ("Failed to register SIGINT handler");
	}
	
	if (sigterm_pipe_fds[0] != -1) {
		GIOChannel *io;
		
		io = g_io_channel_unix_new (sigterm_pipe_fds[0]);
		g_io_channel_set_close_on_unref (io, TRUE);
		
		g_io_add_watch (io, G_IO_IN | G_IO_PRI | G_IO_HUP | G_IO_ERR, quit_handler, NULL);
	}
}

void clean_process (NetworkInadorHandle *handle) {
	Interface *iface;
	
	iface = handle->interfaces;
	
	while (iface != NULL) {
		if (iface->dhcp_info.process_pid != 0) {
			kill (iface->dhcp_info.process_pid, SIGTERM);
		}
	}
}

int main (int argc, char *argv[]) {
	NetworkInadorHandle handle;
	int nl_sock;
	int nl_watch;
	Interface *to_up;
	
	//signal (SIGPIPE, SIG_IGN);
	//signal (SIGCHLD, SIG_IGN);
	
#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif
	
	loop = g_main_loop_new (NULL, FALSE);
	
	setup_signal ();
	
	/* Inicializar nuestra estructura principal */
	handle.interfaces = NULL;
	handle.rtable_v4 = NULL;
	
	/* Crear los sockets de petición */
	nl_sock = create_route_netlink_socket (0);
	handle.netlink_sock_request = nl_sock;
	nl_watch = create_route_netlink_socket (-1);
	
	wireless_init (&handle);
	
	manager_events_setup (&handle);
	manager_setup_socket (&handle);
	
	interfaces_list_all (&handle, nl_sock);
	
	routes_list (&handle, nl_sock);
	
	netlink_events_setup_loop (&handle, nl_watch);
	
	g_main_loop_run (loop);
	
	return 0;
}
