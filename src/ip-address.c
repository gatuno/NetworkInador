/*
 * ip-address.c
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

#include <string.h>

#include <netlink/socket.h>
#include <netlink/msg.h>

#include <net/if_arp.h>
#include <arpa/inet.h>

#include <gmodule.h>

#include "common.h"
#include "ip-address.h"
#include "interfaces.h"

static IPAddr *_ip_address_search_addr (Interface *iface, sa_family_t family, void *addr_data, uint32_t prefix) {
	GList *g;
	IPAddr *addr;
	
	for (g = iface->address; g != NULL; g = g->next) {
		addr = (IPAddr *) g->data;
		
		if (addr->family == family) {
			if (family == AF_INET && memcmp (&addr->sin_addr, addr_data, sizeof (struct in_addr)) == 0 && addr->prefix == prefix) {
				return addr;
			} else if (family == AF_INET6 && memcmp (&addr->sin6_addr, addr_data, sizeof (struct in6_addr)) == 0 && addr->prefix == prefix) {
				return addr;
			}
		}
	}
	
	return NULL;
}

int ip_address_receive_message_newaddr (struct nl_msg *msg, void *arg) {
	struct nlmsghdr *reply;
	struct ifaddrmsg *addr_msg;
	int remaining;
	struct nlattr *attr;
	NetworkInadorHandle *handle = (NetworkInadorHandle *) arg;
	Interface *iface;
	int has_addr = 0;
	struct in_addr sin_addr;
	struct in6_addr sin6_addr;
	IPAddr *addr;
	
	reply = nlmsg_hdr (msg);
	
	if (reply->nlmsg_type != RTM_NEWADDR) return NL_SKIP;
	
	addr_msg = nlmsg_data (reply);
	
	iface = _interfaces_locate_by_index (handle->interfaces, addr_msg->ifa_index);
	
	if (iface == NULL) {
		printf ("IP para una interfaz desconocida\n");
		return NL_SKIP;
	}
	
	nlmsg_for_each_attr(attr, reply, sizeof (struct ifaddrmsg), remaining) {
		if (nla_type (attr) != IFA_ADDRESS) continue;
		
		if (addr_msg->ifa_family == AF_INET && nla_len (attr) == 4) {
			/* IP de ipv4 */
			memcpy (&sin_addr, nla_data (attr), nla_len (attr));
			has_addr = 1;
		} else if (addr_msg->ifa_family == AF_INET6 && nla_len (attr) == 16) {
			/* IP de ipv6 */
			memcpy (&sin6_addr, nla_data (attr), nla_len (attr));
			has_addr = 1;
		}
	}
	
	
	if (addr_msg->ifa_family == AF_INET) {
		addr = _ip_address_search_addr (iface, AF_INET, &sin_addr, addr_msg->ifa_prefixlen);
	} else if (addr_msg->ifa_family == AF_INET6) {
		addr = _ip_address_search_addr (iface, AF_INET6, &sin6_addr, addr_msg->ifa_prefixlen);
	}
	
	if (addr == NULL) {
		addr = g_new0 (IPAddr, 1);
		
		iface->address = g_list_append (iface->address, addr);
		
		addr->family = addr_msg->ifa_family;
		if (addr->family == AF_INET) {
			memcpy (&addr->sin_addr, &sin_addr, sizeof (struct in_addr));
		} else if (addr->family == AF_INET6) {
			memcpy (&addr->sin6_addr, &sin6_addr, sizeof (struct in6_addr));
		}
		
		addr->prefix = addr_msg->ifa_prefixlen;
	}
	
	char buffer[2048];
	inet_ntop (addr->family, &addr->sin_addr, buffer, sizeof (buffer));
	printf ("Dirección IP %s/%d sobre interfaz: %d\n", buffer, addr->prefix, iface->index);
	
	addr->flags = addr_msg->ifa_flags;
	addr->scope = addr_msg->ifa_scope;
	
	return NL_SKIP;
}

int ip_address_receive_message_deladdr (struct nl_msg *msg, void *arg) {
	struct nlmsghdr *reply;
	struct ifaddrmsg *addr_msg;
	int remaining;
	struct nlattr *attr;
	NetworkInadorHandle *handle = (NetworkInadorHandle *) arg;
	Interface *iface;
	struct in_addr sin_addr;
	struct in6_addr sin6_addr;
	IPAddr *addr = NULL;
	
	reply = nlmsg_hdr (msg);
	
	if (reply->nlmsg_type != RTM_DELADDR) return NL_SKIP;
	
	addr_msg = nlmsg_data (reply);
	
	iface = _interfaces_locate_by_index (handle->interfaces, addr_msg->ifa_index);
	
	if (iface == NULL) {
		printf ("IP para una interfaz desconocida\n");
		return NL_SKIP;
	}
	
	nlmsg_for_each_attr(attr, reply, sizeof (struct ifaddrmsg), remaining) {
		if (nla_type (attr) != IFA_ADDRESS) continue;
		
		if (addr_msg->ifa_family == AF_INET && nla_len (attr) == 4) {
			/* IP de ipv4 */
			memcpy (&sin_addr, nla_data (attr), nla_len (attr));
		} else if (addr_msg->ifa_family == AF_INET6 && nla_len (attr) == 16) {
			/* IP de ipv6 */
			memcpy (&sin6_addr, nla_data (attr), nla_len (attr));
		}
	}
	
	
	if (addr_msg->ifa_family == AF_INET) {
		addr = _ip_address_search_addr (iface, AF_INET, &sin_addr, addr_msg->ifa_prefixlen);
	} else if (addr_msg->ifa_family == AF_INET6) {
		addr = _ip_address_search_addr (iface, AF_INET6, &sin6_addr, addr_msg->ifa_prefixlen);
	}
	
	if (addr == NULL) {
		printf ("IP no encontrada\n");
		return NL_SKIP;
	}
	
	/* Eliminar de la lista ligada */
	iface->address = g_list_remove (iface->address, addr);
	
	g_free (addr);
	
	return NL_SKIP;
}

static int _ip_address_wait_ack_or_error (struct nl_msg *msg, void *arg) {
	int *ret = (int *) arg;
	struct nlmsgerr *l_err;
	struct nlmsghdr *reply;
	
	reply = nlmsg_hdr (msg);
	
	if (reply->nlmsg_type == NLMSG_ERROR) {
		l_err = nlmsg_data (reply);
		
		*ret = l_err->error;
	}
	
	return NL_SKIP;
}

static int _ip_address_wait_error (struct sockaddr_nl *nla, struct nlmsgerr *l_err, void *arg) {
	int *ret = (int *) arg;
	
	*ret = l_err->error;
	
	return NL_SKIP;
}

int ip_address_add_ip (NetworkInadorHandle *handle, int index, IPAddr *addr) {
	struct nl_msg * msg;
	struct ifaddrmsg addr_hdr;
	int ret, error;
	Interface *iface;
	
	iface = _interfaces_locate_by_index (handle->interfaces, index);
	
	if (iface == NULL) {
		printf ("Error, solicitaron operación sobre interfaz que no existe\n");
		
		return -1;
	}
	
	addr_hdr.ifa_family = addr->family;
	addr_hdr.ifa_prefixlen = addr->prefix;
	addr_hdr.ifa_flags = addr->flags;
	addr_hdr.ifa_scope = addr->scope;
	addr_hdr.ifa_index = index;
	
	msg = nlmsg_alloc_simple (RTM_NEWADDR, NLM_F_REQUEST);
	ret = nlmsg_append (msg, &addr_hdr, sizeof (addr_hdr), NLMSG_ALIGNTO);
	
	if (ret != 0) {
		nlmsg_free (msg);
		
		return -1;
	}
	
	if (addr->family == AF_INET) {
		ret = nla_put (msg, IFA_LOCAL, sizeof (addr->sin_addr), &addr->sin_addr);
		ret = nla_put (msg, IFA_ADDRESS, sizeof (addr->sin_addr), &addr->sin_addr);
	} else {
		ret = nla_put (msg, IFA_LOCAL, sizeof (addr->sin6_addr), &addr->sin6_addr);
		ret = nla_put (msg, IFA_ADDRESS, sizeof (addr->sin6_addr), &addr->sin6_addr);
	}
	
	if (ret != 0) {
		nlmsg_free (msg);
		
		return -1;
	}
	
	nl_complete_msg (handle->nl_sock_route, msg);
	
	ret = nl_send (handle->nl_sock_route, msg);
	
	nlmsg_free (msg);
	if (ret <= 0) {
		return -1;
	}
	
	error = 0;
	nl_socket_modify_cb (handle->nl_sock_route, NL_CB_VALID, NL_CB_CUSTOM, _ip_address_wait_ack_or_error, &error);
	nl_socket_modify_cb (handle->nl_sock_route, NL_CB_INVALID, NL_CB_CUSTOM, _ip_address_wait_ack_or_error, &error);
	nl_socket_modify_cb (handle->nl_sock_route, NL_CB_ACK, NL_CB_CUSTOM, _ip_address_wait_ack_or_error, &error);
	nl_socket_modify_err_cb (handle->nl_sock_route, NL_CB_CUSTOM, _ip_address_wait_error, &error);
	
	nl_recvmsgs_default (handle->nl_sock_route);
	
	if (ret <= 0 || error < 0) {
		return -1;
	}
	
	return 0;
}

int ip_address_del_ip (NetworkInadorHandle *handle, int index, IPAddr *addr) {
	struct nl_msg * msg;
	struct ifaddrmsg addr_hdr;
	int ret, error;
	Interface *iface;
	GList *addr_pos;
	
	iface = _interfaces_locate_by_index (handle->interfaces, index);
	
	if (iface == NULL) {
		printf ("Error, solicitaron operación sobre interfaz que no existe\n");
		
		return -1;
	}
	
	addr_pos = g_list_find (iface->address, addr);
	
	if (addr_pos == NULL) {
		printf ("Error, la dirección solicitada no pertenece a la interfaz\n");
		
		return -1;
	}
	
	addr_hdr.ifa_family = addr->family;
	addr_hdr.ifa_prefixlen = addr->prefix;
	addr_hdr.ifa_flags = addr->flags;
	addr_hdr.ifa_scope = addr->scope;
	addr_hdr.ifa_index = index;
	
	msg = nlmsg_alloc_simple (RTM_DELADDR, NLM_F_REQUEST);
	ret = nlmsg_append (msg, &addr_hdr, sizeof (addr_hdr), NLMSG_ALIGNTO);
	
	if (ret != 0) {
		nlmsg_free (msg);
		
		return -1;
	}
	
	if (addr->family == AF_INET) {
		ret = nla_put (msg, IFA_LOCAL, sizeof (addr->sin_addr), &addr->sin_addr);
		ret = nla_put (msg, IFA_ADDRESS, sizeof (addr->sin_addr), &addr->sin_addr);
	} else {
		ret = nla_put (msg, IFA_LOCAL, sizeof (addr->sin6_addr), &addr->sin6_addr);
		ret = nla_put (msg, IFA_ADDRESS, sizeof (addr->sin6_addr), &addr->sin6_addr);
	}
	
	if (ret != 0) {
		nlmsg_free (msg);
		
		return -1;
	}
	
	nl_complete_msg (handle->nl_sock_route, msg);
	
	ret = nl_send (handle->nl_sock_route, msg);
	
	nlmsg_free (msg);
	if (ret <= 0) {
		return -1;
	}
	
	error = 0;
	nl_socket_modify_cb (handle->nl_sock_route, NL_CB_VALID, NL_CB_CUSTOM, _ip_address_wait_ack_or_error, &error);
	nl_socket_modify_cb (handle->nl_sock_route, NL_CB_INVALID, NL_CB_CUSTOM, _ip_address_wait_ack_or_error, &error);
	nl_socket_modify_cb (handle->nl_sock_route, NL_CB_ACK, NL_CB_CUSTOM, _ip_address_wait_ack_or_error, &error);
	nl_socket_modify_err_cb (handle->nl_sock_route, NL_CB_CUSTOM, _ip_address_wait_error, &error);
	
	nl_recvmsgs_default (handle->nl_sock_route);
	
	if (ret <= 0 || error < 0) {
		return -1;
	}
	
	return 0;
}

void ip_address_init (NetworkInadorHandle *handle) {
	/* Si es la primera vez que nos llaman, descargar una primera lista de direcciones en todas las interfaces */
	struct nl_msg * msg;
	struct ifaddrmsg addr_hdr = {
		.ifa_family = AF_UNSPEC,
	};
	int ret;
	
	msg = nlmsg_alloc_simple (RTM_GETADDR, NLM_F_REQUEST | NLM_F_DUMP);
	ret = nlmsg_append (msg, &addr_hdr, sizeof (addr_hdr), NLMSG_ALIGNTO);
	
	if (ret != 0) {
		return;
	}
	
	nl_complete_msg (handle->nl_sock_route, msg);
	
	ret = nl_send (handle->nl_sock_route, msg);
	
	nlmsg_free (msg);
	
	nl_socket_modify_cb (handle->nl_sock_route, NL_CB_VALID, NL_CB_CUSTOM, ip_address_receive_message_newaddr, handle);
	
	nl_recvmsgs_default (handle->nl_sock_route);
}

