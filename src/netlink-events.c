/*
 * netlink-events.c
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

#include <netlink/socket.h>
#include <netlink/msg.h>

#include <glib.h>

#include "common.h"
#include "interfaces.h"
#include "ip-address.h"

static int _netlink_events_route_dispatcher (struct nl_msg *msg, void *arg) {
	struct nlmsghdr *reply;
	
	reply = nlmsg_hdr (msg);
	
	switch (reply->nlmsg_type) {
		case RTM_NEWLINK:
			return interface_receive_message_newlink (msg, arg);
			break;
		case RTM_DELLINK:
			return interface_receive_message_dellink (msg, arg);
			break;
		case RTM_NEWADDR:
			return ip_address_receive_message_newaddr (msg, arg);
			break;
		case RTM_DELADDR:
			return ip_address_receive_message_deladdr (msg, arg);
			break;
	}
	
	return NL_SKIP;
}

static gboolean _netlink_events_handle_read (GIOChannel *source, GIOCondition condition, gpointer data) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) data;
	
	nl_recvmsgs_default (handle->nl_sock_route_events);
	return TRUE;
}

void netlink_events_setup (NetworkInadorHandle *handle) {
	struct nl_sock * sock_req;
	int fd;
	GIOChannel *channel;
	
	sock_req = nl_socket_alloc ();
	
	if (nl_connect (sock_req, NETLINK_ROUTE) != 0) {
		perror ("Falló conectar netlink socket para eventos\n");
		
		return;
	}
	
	nl_socket_set_nonblocking (sock_req);
	nl_socket_add_memberships (sock_req, RTNLGRP_LINK, RTNLGRP_IPV4_IFADDR, RTNLGRP_IPV6_IFADDR, RTNLGRP_IPV6_IFINFO, 0);
	nl_socket_disable_seq_check (sock_req);
	nl_socket_modify_cb (sock_req, NL_CB_VALID, NL_CB_CUSTOM, _netlink_events_route_dispatcher, handle);
	
	fd = nl_socket_get_fd (sock_req);
	
	channel = g_io_channel_unix_new (fd);
	
	handle->route_events_source = g_io_add_watch (channel, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, _netlink_events_handle_read, handle);
	handle->nl_sock_route_events = sock_req;
}

void netlink_events_clear (NetworkInadorHandle *handle) {
	/* Primero, detener los eventos del source watch */
	
	g_source_remove (handle->route_events_source);
	
	handle->route_events_source = 0;
	/* Cerrar el socket */
	
	nl_close (handle->nl_sock_route_events);
	handle->nl_sock_route_events = NULL;
}

