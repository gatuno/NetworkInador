/*
 * events.c
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

#include <stdint.h>

#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/if_addr.h>

#include <linux/wireless.h>

#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "events.h"
#include "network-inador.h"
#include "interfaces.h"

#include <glib.h>

static gboolean _events_handle_read (GIOChannel *source, GIOCondition condition, gpointer data) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) data;
	int sock;
	
	sock = g_io_channel_unix_get_fd (source);
	
	char reply[8192]; /* a large buffer */
	struct sockaddr_nl kernel;
	int len;
	
	struct iovec io;
	/* Para la respuesta */
	struct nlmsghdr *msg_ptr;    /* pointer to current part */
	struct msghdr rtnl_reply;    /* generic msghdr structure */
	struct iovec io_reply;
	
	/* Esperar la respuesta */
	memset(&io_reply, 0, sizeof(io_reply));
	memset(&rtnl_reply, 0, sizeof(rtnl_reply));
	
	io.iov_base = reply;
	io.iov_len = sizeof (reply);
	rtnl_reply.msg_iov = &io;
	rtnl_reply.msg_iovlen = 1;
	rtnl_reply.msg_name = &kernel;
	rtnl_reply.msg_namelen = sizeof(kernel);
	
	len = recvmsg(sock, &rtnl_reply, 0);
	
	if (len == 0) {
		printf ("Lectura de eventos regresó 0\n");
		return FALSE;
	} else if (len < 0) {
		perror ("Error en recvmsg\n");
		return TRUE;
	}
	
	msg_ptr = (struct nlmsghdr *) reply;

	for (; NLMSG_OK(msg_ptr, len); msg_ptr = NLMSG_NEXT(msg_ptr, len)) {
		printf ("Msg type: %i\n", msg_ptr->nlmsg_type);
		if (msg_ptr->nlmsg_type == RTM_NEWLINK) {
			printf ("Mensaje dinámico de nueva interfaz\n");
			interfaces_add_or_update_rtnl_link (handle, msg_ptr, 0);
		} else if (msg_ptr->nlmsg_type == RTM_DELLINK) {
			printf ("Mensaje dinámico de eliminar interfaz\n");
			interfaces_del_rtnl_link (handle, msg_ptr);
		} else if (msg_ptr->nlmsg_type == RTM_NEWADDR) {
			printf ("Mensaje dinámico de nueva IP\n");
			interfaces_add_or_update_ipv4 (handle, msg_ptr);
		} else if (msg_ptr->nlmsg_type == RTM_DELADDR) {
			printf ("Mensaje dinámico de eliminar IP\n");
			interfaces_del_ipv4 (handle, msg_ptr);
		}
	}
	
	
	return TRUE;
}

void events_setup_loop (NetworkInadorHandle *handle, int sock) {
	GIOChannel *channel;
	
	channel = g_io_channel_unix_new (sock);
	
	g_io_add_watch (channel, G_IO_IN | G_IO_PRI, _events_handle_read, handle);
}

