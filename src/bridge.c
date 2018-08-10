/*
 * bridge.c
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

#include <sys/types.h>
#include <sys/socket.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/if_addr.h>

#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "interfaces.h"
#include "rta_aux.h"

void bridge_create (int sock, const char *name) {
	struct msghdr rtnl_msg;
	struct iovec io;
	struct sockaddr_nl kernel;
	char buffer[8192];
	int len;
	struct nlmsghdr *nl;
	struct ifinfomsg *ifi;
	struct rtattr *linkinfo;
	struct nlmsgerr *l_err;
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	if (name != NULL) {
		/* Validar la longitud del nombre, por seguridad */
		len = strlen (name) + 1;
		if (len == 1) {
			/* Nombre muy corto */
			return;
		}
		if (len > IFNAMSIZ) {
			/* Nombre muy largo */
			return;
		}
	}
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (sock, (struct sockaddr *) &local_nl, &local_size);
	
	memset (&kernel, 0, sizeof (kernel));
	memset (buffer, 0, sizeof (buffer));
	memset (&io, 0, sizeof (io));
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	
	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	nl = (struct nlmsghdr *) buffer;
	nl->nlmsg_len = NLMSG_LENGTH (sizeof(struct ifinfomsg));
	nl->nlmsg_type = RTM_NEWLINK;
	nl->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
	nl->nlmsg_seq = global_nl_seq++;
	nl->nlmsg_pid = local_nl.nl_pid;
	
	ifi = (struct ifinfomsg*) NLMSG_DATA (nl);
	ifi->ifi_family = AF_UNSPEC;
	ifi->ifi_index = 0;
	
	/* Si existe un nombre, anexar como atributo */
	if (name != NULL) {
		rta_addattr_l (nl, sizeof (buffer), IFLA_IFNAME, name, strlen (name));
	}
	
	linkinfo = rta_addattr_nest (nl, sizeof (buffer), IFLA_LINKINFO);
	rta_addattr_l (nl, sizeof (buffer), IFLA_INFO_KIND, "bridge", strlen ("bridge"));
	rta_addattr_nest_end (nl, linkinfo);
	
	io.iov_base = buffer;
	io.iov_len = nl->nlmsg_len;
	
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	len = sendmsg (sock, (struct msghdr *) &rtnl_msg, 0);
	
	/* Esperar la respuesta */
	memset (&io, 0, sizeof (io));
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	memset (buffer, 0, sizeof (buffer));
	
	io.iov_base = buffer;
	io.iov_len = sizeof (buffer);
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	len = recvmsg(sock, &rtnl_msg, 0);
	nl = (struct nlmsghdr *) buffer;
	for (; NLMSG_OK(nl, len); nl = NLMSG_NEXT(nl, len)) {
		if (nl->nlmsg_type == NLMSG_DONE) {
			printf ("Bridge ADD Msg type: DONE!\n");
			break;
		}
		if (nl->nlmsg_type == NLMSG_ERROR) {
			l_err = (struct nlmsgerr*) NLMSG_DATA (nl);
			if (nl->nlmsg_len < NLMSG_LENGTH (sizeof (struct nlmsgerr))) {
				printf ("Bridge ADD Error tamaño truncado\n");
			} else if (l_err->error != 0) {
				// Error:
				printf ("Bridge ADD Error: %i\n", l_err->error);
			}
			break;
		}
	}
}

void bridge_add_interface (int sock, Interface *bridge, Interface *slave) {
	struct msghdr rtnl_msg;
	struct iovec io;
	struct sockaddr_nl kernel;
	char buffer[8192];
	int len;
	struct nlmsghdr *nl;
	struct ifinfomsg *ifi;
	struct nlmsgerr *l_err;
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (sock, (struct sockaddr *) &local_nl, &local_size);
	
	memset (&kernel, 0, sizeof (kernel));
	memset (buffer, 0, sizeof (buffer));
	memset (&io, 0, sizeof (io));
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	
	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	nl = (struct nlmsghdr *) buffer;
	nl->nlmsg_len = NLMSG_LENGTH (sizeof(struct ifinfomsg));
	nl->nlmsg_type = RTM_NEWLINK;
	nl->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nl->nlmsg_seq = global_nl_seq++;
	nl->nlmsg_pid = local_nl.nl_pid;
	
	ifi = (struct ifinfomsg*) NLMSG_DATA (nl);
	ifi->ifi_family = AF_UNSPEC;
	ifi->ifi_index = slave->index;
	
	rta_addattr_l (nl, sizeof (buffer), IFLA_MASTER, &bridge->index, 4);
	
	io.iov_base = buffer;
	io.iov_len = nl->nlmsg_len;
	
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	len = sendmsg (sock, (struct msghdr *) &rtnl_msg, 0);
	
	/* Esperar la respuesta */
	memset (&io, 0, sizeof (io));
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	memset (buffer, 0, sizeof (buffer));
	
	io.iov_base = buffer;
	io.iov_len = sizeof (buffer);
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	len = recvmsg(sock, &rtnl_msg, 0);
	nl = (struct nlmsghdr *) buffer;
	for (; NLMSG_OK(nl, len); nl = NLMSG_NEXT(nl, len)) {
		if (nl->nlmsg_type == NLMSG_DONE) {
			printf ("Bridge ADD Slave Msg type: DONE!\n");
			break;
		}
		if (nl->nlmsg_type == NLMSG_ERROR) {
			l_err = (struct nlmsgerr*) NLMSG_DATA (nl);
			if (nl->nlmsg_len < NLMSG_LENGTH (sizeof (struct nlmsgerr))) {
				printf ("Bridge ADD Slave Error tamaño truncado\n");
			} else if (l_err->error != 0) {
				// Error:
				printf ("Bridge ADD Slave Error: %i\n", l_err->error);
			}
			break;
		}
	}
}

void bridge_remove_slave_from_bridge (int sock, Interface *slave) {
	struct msghdr rtnl_msg;
	struct iovec io;
	struct sockaddr_nl kernel;
	char buffer[8192];
	int len;
	struct nlmsghdr *nl;
	struct ifinfomsg *ifi;
	struct nlmsgerr *l_err;
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (sock, (struct sockaddr *) &local_nl, &local_size);
	
	memset (&kernel, 0, sizeof (kernel));
	memset (buffer, 0, sizeof (buffer));
	memset (&io, 0, sizeof (io));
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	
	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	nl = (struct nlmsghdr *) buffer;
	nl->nlmsg_len = NLMSG_LENGTH (sizeof(struct ifinfomsg));
	nl->nlmsg_type = RTM_NEWLINK;
	nl->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nl->nlmsg_seq = global_nl_seq++;
	nl->nlmsg_pid = local_nl.nl_pid;
	
	ifi = (struct ifinfomsg*) NLMSG_DATA (nl);
	ifi->ifi_family = AF_UNSPEC;
	ifi->ifi_index = slave->index;
	
	int ifindex = 0;
	rta_addattr_l (nl, sizeof (buffer), IFLA_MASTER, &ifindex, 4);
	
	io.iov_base = buffer;
	io.iov_len = nl->nlmsg_len;
	
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	len = sendmsg (sock, (struct msghdr *) &rtnl_msg, 0);
	
	/* Esperar la respuesta */
	memset (&io, 0, sizeof (io));
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	memset (buffer, 0, sizeof (buffer));
	
	io.iov_base = buffer;
	io.iov_len = sizeof (buffer);
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	len = recvmsg(sock, &rtnl_msg, 0);
	nl = (struct nlmsghdr *) buffer;
	for (; NLMSG_OK(nl, len); nl = NLMSG_NEXT(nl, len)) {
		if (nl->nlmsg_type == NLMSG_DONE) {
			printf ("Bridge NOMaster Msg type: DONE!\n");
			break;
		}
		if (nl->nlmsg_type == NLMSG_ERROR) {
			l_err = (struct nlmsgerr*) NLMSG_DATA (nl);
			if (nl->nlmsg_len < NLMSG_LENGTH (sizeof (struct nlmsgerr))) {
				printf ("Bridge NOMaster Error tamaño truncado\n");
			} else if (l_err->error != 0) {
				// Error:
				printf ("Bridge NOMaster Error: %i\n", l_err->error);
			}
			break;
		}
	}
}
