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

#include <linux/netlink.h>
#include <linux/if.h>

#include <net/ethernet.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>

#include "network-inador.h"
#include "interfaces.h"
#include "events.h"
#include "manager.h"

static GMainLoop *loop = NULL;

int create_ntlink_socket (int groups) {
	int fd;
	struct sockaddr_nl local;
	
	fd = socket (AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	
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
	
	return fd;
}

int main (int argc, char *argv[]) {
	NetworkInadorHandle handle;
	int nl_sock;
	int nl_watch;
	
	signal (SIGPIPE, SIG_IGN);
	
#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif
	
	loop = g_main_loop_new (NULL, FALSE);
	
	handle.interfaces = NULL;
	
	Interface *to_up;
	
	nl_sock = create_ntlink_socket (0);
	handle.netlink_sock_request = nl_sock;
	nl_watch = create_ntlink_socket (-1);
	
	interfaces_list_all (&handle, nl_sock);
	
	events_setup_loop (&handle, nl_watch);
	
	manager_setup_socket (&handle);
	
	g_main_loop_run (loop);
	
	return 0;
}
