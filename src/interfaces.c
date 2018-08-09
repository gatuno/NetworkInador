/*
 * interfaces.c
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

#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "interfaces.h"

Interface * interfaces_locate_by_index (Interface *list, int index);
static void _interfaces_append_ipv4_to_struct (Interface *interface, struct in_addr address, uint32_t prefix);

int global_nl_seq = 1;

typedef struct {
	struct nlmsghdr hdr;
	struct rtgenmsg gen;
} nl_req_t;

Interface * interfaces_locate_by_index (Interface *list, int index) {
	Interface *iface;
	
	iface = list;
	
	while (iface != NULL) {
		if (iface->index == index) {
			return iface;
		}
		iface = iface->next;
	}
	
	return NULL;
}

static void _interfaces_append_ipv4_to_struct (Interface *interface, struct in_addr address, uint32_t prefix) {
	IPv4 *new_addr, *last;
	
	new_addr = (IPv4 *) malloc (sizeof (IPv4));
	
	new_addr->sin_addr = address;
	new_addr->prefix = prefix;
	
	new_addr->next = NULL;
	
	if (interface->v4_address == NULL) {
		interface->v4_address = new_addr;
	} else {
		last = interface->v4_address;
		
		while (last->next != NULL) {
			last = last->next;
		}
		
		last->next = new_addr;
	}
}

static IPv4 * _interfaces_serach_ipv4 (Interface *interface, struct in_addr address, uint32_t prefix) {
	IPv4 *list;
	
	list = interface->v4_address;
	
	while (list != NULL) {
		if (list->sin_addr.s_addr == address.s_addr && list->prefix == prefix) {
			return list;
		}
		list = list->next;
	}
	
	return NULL;
}

static void _interfaces_list_ipv4_address (NetworkInadorHandle *handle, int sock) {
	struct msghdr rtnl_msg;    /* generic msghdr struct for use with sendmsg */
	struct iovec io;
	nl_req_t req;
	struct sockaddr_nl kernel;
	char reply[8192]; /* a large buffer */
	int len;
	
	/* Para la respuesta */
	struct nlmsghdr *msg_ptr;    /* pointer to current part */
	
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (sock, (struct sockaddr *) &local_nl, &local_size);
	
	memset(&rtnl_msg, 0, sizeof(rtnl_msg));
	memset(&kernel, 0, sizeof(kernel));
	memset(&req, 0, sizeof(req));

	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
	req.hdr.nlmsg_type = RTM_GETADDR;
	req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP; 
	req.hdr.nlmsg_seq = global_nl_seq++;
	req.hdr.nlmsg_pid = local_nl.nl_pid;
	req.gen.rtgen_family = AF_INET;

	io.iov_base = &req;
	io.iov_len = req.hdr.nlmsg_len;
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	sendmsg (sock, (struct msghdr *) &rtnl_msg, 0);
	
	/* Esperar la respuesta */
	memset(&io, 0, sizeof(io));
	memset(&rtnl_msg, 0, sizeof(rtnl_msg));
	
	io.iov_base = reply;
	io.iov_len = sizeof (reply);
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);

	while ((len = recvmsg(sock, &rtnl_msg, 0)) > 0) { /* read lots of data */
		msg_ptr = (struct nlmsghdr *) reply;
		if (msg_ptr->nlmsg_type == NLMSG_DONE) break;
	
		for (; NLMSG_OK(msg_ptr, len); msg_ptr = NLMSG_NEXT(msg_ptr, len)) {
			/* Procesar solo los mensajes de nueva interfaz */
			printf ("Msg type: %i\n", msg_ptr->nlmsg_type);
			if (msg_ptr->nlmsg_type == RTM_NEWADDR) {
				interfaces_add_or_update_ipv4 (handle, msg_ptr);
			}
		}
	}
}

void interfaces_add_or_update_rtnl_link (NetworkInadorHandle *handle, struct nlmsghdr *h) {
	struct ifinfomsg *iface;
	struct rtattr *attribute;
	int len;
	Interface *new, *last;
	
	iface = NLMSG_DATA(h);
	len = h->nlmsg_len - NLMSG_LENGTH (sizeof (struct ifinfomsg));
	
	printf ("Mensaje de nueva interfaz\n");
	new = interfaces_locate_by_index (handle->interfaces, iface->ifi_index);
	
	/* Si el objeto interface no existe, crearlo y ligarlo en la lista de interfaces */
	if (new == NULL) {
		printf ("Creando...\n");
		new = malloc (sizeof (Interface));
		memset (new, 0, sizeof (Interface));
		new->next = NULL;
		
		if (handle->interfaces == NULL) {
			handle->interfaces = new;
		} else {
			last = handle->interfaces;
			
			while (last->next != NULL) {
				last = last->next;
			}
			
			last->next = new;
		}
	}
	
	new->ifi_type = iface->ifi_type;
	/* TODO: Checar aquí cambio de flags */
	new->flags = iface->ifi_flags;
	new->index = iface->ifi_index;
	
	if (iface->ifi_type == ARPHRD_LOOPBACK) {
		/* Es loopback */
		new->is_loopback = 1;
	}
	
	if (iface->ifi_type == ARPHRD_ETHER) {
		/* Es ethernet */
	}
	
	for (attribute = IFLA_RTA(iface); RTA_OK(attribute, len); attribute = RTA_NEXT(attribute, len)) {
		//printf ("Attribute: %d\n", attribute->rta_type);
		switch(attribute->rta_type) {
			case IFLA_IFNAME: 
				//printf ("Interface %d : %s\n", iface->ifi_index, (char *) RTA_DATA(attribute));
				// Actualizar el nombre de la interfaz */
				strncpy (new->name, RTA_DATA (attribute), IFNAMSIZ);
				break;
			case IFLA_ADDRESS:
				/* FIXME: ¿Debería no actualizar la mac address siempre? */
				memcpy (new->real_hw, RTA_DATA (attribute), ETHER_ADDR_LEN);
				//printf ("Interface %d has hw addr: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\n", iface->ifi_index, new->real_hw[0], new->real_hw[1], new->real_hw[2], new->real_hw[3], new->real_hw[4], new->real_hw[5]);
				break;
			case IFLA_WIRELESS:
				/* TODO: Procesar enlaces los mensajes de enlaces inalambricos */
				break;
			//default:
				//printf ("RTA Attribute \"%i\" no procesado\n", attribute->rta_type);
		}
	}
}

void interfaces_del_rtnl_link (NetworkInadorHandle *handle, struct nlmsghdr *h) {
	struct ifinfomsg *iface;
	Interface *to_del, *last;
	IPv4 *address;
	iface = NLMSG_DATA(h);
	
	to_del = interfaces_locate_by_index (handle->interfaces, iface->ifi_index);
	
	if (to_del == NULL) {
		printf ("Error, solicitaron eliminar interfaz que ya no existe\n");
		
		return;
	}
	
	address = to_del->v4_address;
	
	while (address != NULL) {
		to_del->v4_address = address->next;
		
		free (address);
		
		address = to_del->v4_address;
	}
	
	if (to_del == handle->interfaces) {
		/* Primero de la lista */
		
		handle->interfaces = to_del->next;
	} else {
		last = handle->interfaces;
		
		while (last->next != to_del) {
			last = last->next;
		}
		
		last->next = to_del->next;
	}
	
	free (to_del);
}

void interfaces_add_or_update_ipv4 (NetworkInadorHandle *handle, struct nlmsghdr *h) {
	struct ifaddrmsg *addr;
	struct rtattr *attribute;
	struct in_addr ip;
	char ip_as_string[1024];
	Interface *iface;
	IPv4 *new;
	uint32_t prefix;
	
	addr = NLMSG_DATA(h);
	
	prefix = addr->ifa_prefixlen;
	
	iface = interfaces_locate_by_index (handle->interfaces, addr->ifa_index);
	
	if (iface == NULL) {
		/* No encuentro la interfaz... */
		return;
	}
	
	printf ("IP para la interfaz: %d\n", addr->ifa_index);
	size_t len = NLMSG_PAYLOAD(h, h->nlmsg_len);
	
	for (attribute = IFA_RTA (addr); RTA_OK (attribute, len); attribute = RTA_NEXT (attribute, len)) {
		//printf ("Attribute (addr): %d\n", attribute->rta_type);
		
		if (attribute->rta_type == IFA_LOCAL) {
			memcpy (&ip, RTA_DATA (attribute), sizeof (struct in_addr));
			
			inet_ntop (AF_INET, &ip, ip_as_string, sizeof (ip_as_string));
			printf ("Address: %s/%d\n", ip_as_string, addr->ifa_prefixlen);
			break;
		}
	}
	
	new = _interfaces_serach_ipv4 (iface, ip, prefix);
	
	if (new == NULL) {
		printf ("Agregando IP a la lista de IP's\n");
		_interfaces_append_ipv4_to_struct (iface, ip, prefix);
	}
}

void interfaces_del_ipv4 (NetworkInadorHandle *handle, struct nlmsghdr *h) {
	struct ifaddrmsg *addr;
	struct rtattr *attribute;
	struct in_addr ip;
	char ip_as_string[1024];
	Interface *iface;
	IPv4 *new, *before;
	uint32_t prefix;
	
	addr = NLMSG_DATA(h);
	
	prefix = addr->ifa_prefixlen;
	
	iface = interfaces_locate_by_index (handle->interfaces, addr->ifa_index);
	
	if (iface == NULL) {
		/* No encuentro la interfaz... */
		return;
	}
	
	printf ("IP eliminada para la interfaz: %d\n", addr->ifa_index);
	size_t len = NLMSG_PAYLOAD(h, h->nlmsg_len);
	
	for (attribute = IFA_RTA (addr); RTA_OK (attribute, len); attribute = RTA_NEXT (attribute, len)) {
		//printf ("Attribute (addr): %d\n", attribute->rta_type);
		
		if (attribute->rta_type == IFA_LOCAL) {
			memcpy (&ip, RTA_DATA (attribute), sizeof (struct in_addr));
			
			inet_ntop (AF_INET, &ip, ip_as_string, sizeof (ip_as_string));
			printf ("Address: %s/%d\n", ip_as_string, addr->ifa_prefixlen);
			break;
		}
	}
	
	new = _interfaces_serach_ipv4 (iface, ip, prefix);
	
	if (new == NULL) {
		printf ("Me solicitaron eliminar la IP y NO existe\n");
	} else {
		if (new == iface->v4_address) {
			iface->v4_address = new->next;
		} else {
			before = iface->v4_address;
			
			while (before->next != new) {
				before = before->next;
			}
			
			before->next = new->next;
		}
		
		free (new);
	}
}

void interfaces_manual_del_ipv4 (int sock, Interface *interface, IPv4 *address) {
	struct msghdr rtnl_msg;
	struct iovec io;
	struct sockaddr_nl kernel;
	char buffer[8192];
	int len;
	struct nlmsghdr *nl;
	struct ifaddrmsg *ifa;
	struct rtattr *rta;
	struct nlmsgerr *l_err;
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (sock, (struct sockaddr *) &local_nl, &local_size);
	
	memset (&kernel, 0, sizeof (kernel));
	memset (buffer, 0, sizeof (buffer));
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	memset (&io, 0, sizeof (io));
	
	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	nl = (struct nlmsghdr *) buffer;
	nl->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
	nl->nlmsg_type = RTM_DELADDR;
	nl->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nl->nlmsg_seq = global_nl_seq++;
	nl->nlmsg_pid = local_nl.nl_pid;
	
	ifa = (struct ifaddrmsg*) NLMSG_DATA (nl);
	ifa->ifa_family = AF_INET; // we only get ipv4 address here
	ifa->ifa_prefixlen = address->prefix;
	ifa->ifa_flags = IFA_F_PERMANENT;
	ifa->ifa_scope = 0;
	ifa->ifa_index = interface->index;
	
	rta = (struct rtattr*) IFA_RTA(ifa);
	rta->rta_type = IFA_LOCAL;
	memcpy (RTA_DATA(rta), &address->sin_addr, sizeof (struct in_addr));
	rta->rta_len = RTA_LENGTH(sizeof (struct in_addr));
	// update nlmsghdr length
	nl->nlmsg_len = NLMSG_ALIGN(nl->nlmsg_len) + rta->rta_len;
	
	// del interface address
	len = sizeof (buffer) - nl->nlmsg_len;
	rta = (struct rtattr*) RTA_NEXT (rta, len);
	rta->rta_type = IFA_ADDRESS;
	memcpy (RTA_DATA(rta), &address->sin_addr, sizeof (struct in_addr));
	rta->rta_len = RTA_LENGTH(sizeof (struct in_addr));
	// update nlmsghdr length
	nl->nlmsg_len += rta->rta_len;
	
	io.iov_base = buffer;
	io.iov_len = nl->nlmsg_len;
	
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	len = sendmsg (sock, (struct msghdr *) &rtnl_msg, 0);
	
	/* Esperar la respuesta */
	memset(&io, 0, sizeof(io));
	memset(&rtnl_msg, 0, sizeof(rtnl_msg));
	
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
			printf ("DEL IP Msg type: DONE!\n");
			break;
		}
		if (nl->nlmsg_type == NLMSG_ERROR) {
			l_err = (struct nlmsgerr*) NLMSG_DATA (nl);
			if (nl->nlmsg_len < NLMSG_LENGTH (sizeof (struct nlmsgerr))) {
				printf ("DEL IP Error tamaño truncado\n");
			} else if (l_err->error != 0) {
				// Error:
				printf ("DEL IP Error: %i\n", l_err->error);
			}
			break;
		}
	}
}

void interfaces_manual_add_ipv4 (int sock, Interface *interface, IPv4 *address) {
	struct msghdr rtnl_msg;
	struct iovec io;
	struct sockaddr_nl kernel;
	char buffer[8192];
	int len;
	struct nlmsghdr *nl;
	struct ifaddrmsg *ifa;
	struct rtattr *rta;
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
	nl->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
	nl->nlmsg_type = RTM_NEWADDR;
	nl->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nl->nlmsg_seq = global_nl_seq++;
	nl->nlmsg_pid = local_nl.nl_pid;
	
	ifa = (struct ifaddrmsg*) NLMSG_DATA (nl);
	ifa->ifa_family = AF_INET; // we only get ipv4 address here
	ifa->ifa_prefixlen = address->prefix;
	ifa->ifa_flags = IFA_F_PERMANENT;
	ifa->ifa_scope = 0;
	ifa->ifa_index = interface->index;
	
	rta = (struct rtattr*) IFA_RTA(ifa);
	rta->rta_type = IFA_LOCAL;
	memcpy (RTA_DATA(rta), &address->sin_addr, sizeof (struct in_addr));
	rta->rta_len = RTA_LENGTH(sizeof (struct in_addr));
	// update nlmsghdr length
	nl->nlmsg_len = NLMSG_ALIGN(nl->nlmsg_len) + rta->rta_len;
	
	// del interface address
	len = sizeof (buffer) - nl->nlmsg_len;
	rta = (struct rtattr*) RTA_NEXT (rta, len);
	rta->rta_type = IFA_ADDRESS;
	memcpy (RTA_DATA(rta), &address->sin_addr, sizeof (struct in_addr));
	rta->rta_len = RTA_LENGTH(sizeof (struct in_addr));
	// update nlmsghdr length
	nl->nlmsg_len += rta->rta_len;
	
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
			printf ("Add IP Msg type: DONE!\n");
			break;
		}
		if (nl->nlmsg_type == NLMSG_ERROR) {
			l_err = (struct nlmsgerr*) NLMSG_DATA (nl);
			if (nl->nlmsg_len < NLMSG_LENGTH (sizeof (struct nlmsgerr))) {
				printf ("Add IP Error tamaño truncado\n");
			} else if (l_err->error != 0) {
				// Error:
				printf ("Add IP Error: %i\n", l_err->error);
			}
			break;
		}
	}
}

void interfaces_clear_all_ipv4_address (NetworkInadorHandle *handle, Interface *interface) {
	IPv4 *address;
	
	address = interface->v4_address;
	
	while (address != NULL) {
		interfaces_manual_del_ipv4 (handle->netlink_sock_request, interface, address);
		address = address->next;
	}
}

void interfaces_list_all (NetworkInadorHandle *handle, int sock) {
	struct msghdr rtnl_msg;    /* generic msghdr struct for use with sendmsg */
	struct iovec io;
	nl_req_t req;
	struct sockaddr_nl kernel;
	char reply[8192]; /* a large buffer */
	int len;
	
	/* Para la respuesta */
	struct nlmsghdr *msg_ptr;    /* pointer to current part */
	
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (sock, (struct sockaddr *) &local_nl, &local_size);
	
	memset(&rtnl_msg, 0, sizeof(rtnl_msg));
	memset(&kernel, 0, sizeof(kernel));
	memset(&req, 0, sizeof(req));

	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
	req.hdr.nlmsg_type = RTM_GETLINK;
	req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP; 
	req.hdr.nlmsg_seq = global_nl_seq++;
	req.hdr.nlmsg_pid = local_nl.nl_pid;
	req.gen.rtgen_family = AF_PACKET; /*  no preferred AF, we will get *all* interfaces */

	io.iov_base = &req;
	io.iov_len = req.hdr.nlmsg_len;
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	sendmsg (sock, (struct msghdr *) &rtnl_msg, 0);
	
	/* Esperar la respuesta */
	memset(&io, 0, sizeof(io));
	memset(&rtnl_msg, 0, sizeof(rtnl_msg));
	
	io.iov_base = reply;
	io.iov_len = sizeof (reply);
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);

	while ((len = recvmsg(sock, &rtnl_msg, 0)) > 0) { /* read lots of data */
		msg_ptr = (struct nlmsghdr *) reply;
		if (msg_ptr->nlmsg_type == NLMSG_DONE) break;
	
		for (; NLMSG_OK(msg_ptr, len); msg_ptr = NLMSG_NEXT(msg_ptr, len)) {
			/* Como listamos interfaces, buscamos todos los mensajes RTM_NEWLINK */
			if (msg_ptr->nlmsg_type == RTM_NEWLINK) {
				interfaces_add_or_update_rtnl_link (handle, msg_ptr);
			}
		}
	}
	
	_interfaces_list_ipv4_address (handle, sock);
}


