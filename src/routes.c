/*
 * routes.c
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

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include "network-inador.h"
#include "interfaces.h"
#include "rta_aux.h"

static uint32_t _routes_utils_ip4_prefix_to_netmask (uint32_t prefix) {
	return prefix < 32 ? ~htonl(0xFFFFFFFF >> prefix) : 0xFFFFFFFF;
}

static Routev4 * _routes_append_routev4_to_struct (NetworkInadorHandle *handle, struct in_addr dest, uint32_t prefix) {
	Routev4 *new_route, *last;
	
	new_route = (Routev4 *) malloc (sizeof (Routev4));
	
	memset (new_route, 0, sizeof (Routev4));
	
	new_route->dest = dest;
	new_route->prefix = prefix;
	
	new_route->next = NULL;
	
	if (handle->rtable_v4 == NULL) {
		handle->rtable_v4 = new_route;
	} else {
		last = handle->rtable_v4;
		
		while (last->next != NULL) {
			last = last->next;
		}
		
		last->next = new_route;
	}
	
	return new_route;
}

static Routev4 * _routes_search_ipv4 (NetworkInadorHandle *handle, struct in_addr dest, uint32_t prefix, unsigned int index) {
	Routev4 *list;
	
	list = handle->rtable_v4;
	
	while (list != NULL) {
		if (list->dest.s_addr == dest.s_addr &&
		    list->prefix == prefix &&
		    list->index == index) {
			return list;
		}
		list = list->next;
	}
	
	return NULL;
}

static void _routes_delete_by_chain (NetworkInadorHandle *handle, Routev4 *route) {
	Routev4 *list, *next, *before;
	struct in_addr mask;
	struct in_addr with_mask;
	struct in_addr dest_masked;
	
	/* Si esta es una ruta local sin gateway, lo más seguro es que se eliminó una IP,
	 * hay que eliminar todas las rutas que contengan un gateway que haga match
	 * con el destino de la ruta y la interfaz
	 * Esto asegura que se eliminen rutas como 0.0.0.0/0
	 */
	
	if (route->gateway.s_addr != 0) {
		return;
	}
	
	list = handle->rtable_v4;
	
	mask.s_addr = _routes_utils_ip4_prefix_to_netmask (route->prefix);
	dest_masked.s_addr = route->dest.s_addr & mask.s_addr;
	
	while (list != NULL) {
		next = list->next;
		
		if (list == route) {
			/* Solo por seguridad, la ruta ya debería estar desligada de la lista */
			list = next;
			continue;
		}
		
		if (list->index != route->index) {
			/* Son de diferentes interfaces, no aplica */
			list = next;
			continue;
		}
		
		if (list->gateway.s_addr != 0) {
			with_mask.s_addr = list->gateway.s_addr & mask.s_addr;
			
			if (with_mask.s_addr == dest_masked.s_addr) {
				/* Eres una ruta que será eliminada */
				if (list == handle->rtable_v4) {
					/* Excelente, primera de la lista */
					handle->rtable_v4 = list->next;
				} else {
					before = handle->rtable_v4;
					
					while (before->next != list) {
						before = before->next;
					}
					
					before->next = list->next;
				}
				
				printf ("Ruta eliminada en cadena\n");
				free (list);
			}
		}
		list = next;
	}
}

void routes_manual_add_ipv4 (int sock, Interface *interface, IPv4 *dest, struct in_addr gateway) {
	struct msghdr rtnl_msg;
	struct iovec io;
	struct sockaddr_nl kernel;
	char buffer[8192];
	int len;
	struct nlmsghdr *nl;
	struct rtmsg *route_addr;
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
	nl->nlmsg_len = NLMSG_LENGTH (sizeof (struct rtmsg));
	nl->nlmsg_type = RTM_NEWROUTE;
	nl->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_REPLACE | NLM_F_CREATE;
	nl->nlmsg_seq = global_nl_seq++;
	nl->nlmsg_pid = local_nl.nl_pid;
	
	route_addr = (struct rtmsg *) NLMSG_DATA (nl);
	
	route_addr->rtm_family = AF_INET;
	route_addr->rtm_table = RT_TABLE_MAIN;
	route_addr->rtm_protocol = RTPROT_STATIC;
	route_addr->rtm_scope = RT_SCOPE_UNIVERSE;
	route_addr->rtm_type = RTN_UNICAST;
	route_addr->rtm_dst_len = dest->prefix;
	
	rta = (struct rtattr *) RTM_RTA (route_addr);
	rta->rta_type = RTA_GATEWAY;
	memcpy (RTA_DATA (rta), &gateway, sizeof (gateway));
	rta->rta_len = RTA_LENGTH (sizeof (gateway));
	nl->nlmsg_len = NLMSG_ALIGN (nl->nlmsg_len) + rta->rta_len;
	
	len = sizeof (buffer) - nl->nlmsg_len;
	rta = (struct rtattr*) RTA_NEXT (rta, len);
	rta->rta_type = RTA_OIF;
	rta->rta_len = RTA_LENGTH (sizeof (int));
	*((int*)RTA_DATA(rta)) = interface->index;
	nl->nlmsg_len += rta->rta_len;
	
	if (dest->prefix != 0) {
		/* Agregar el atributo destino */
		len = sizeof (buffer) - nl->nlmsg_len;
		rta = (struct rtattr*) RTA_NEXT (rta, len);
		rta->rta_type = RTA_DST;
		rta->rta_len = RTA_LENGTH (sizeof (struct in_addr));
		memcpy (RTA_DATA(rta), &dest->sin_addr, sizeof (struct in_addr));
		nl->nlmsg_len += rta->rta_len;
	}
	
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
			printf ("Route ADD Msg type: DONE!\n");
			break;
		}
		if (nl->nlmsg_type == NLMSG_ERROR) {
			l_err = (struct nlmsgerr*) NLMSG_DATA (nl);
			if (nl->nlmsg_len < NLMSG_LENGTH (sizeof (struct nlmsgerr))) {
				printf ("Route ADD Error tamaño truncado\n");
			} else if (l_err->error != 0) {
				// Error:
				printf ("Route ADD Error: %i\n", l_err->error);
			}
			break;
		}
	}
}

void routes_manual_del_v4 (int sock, Routev4 *route) {
	struct msghdr rtnl_msg;
	struct iovec io;
	struct sockaddr_nl kernel;
	char buffer[8192];
	int len;
	struct nlmsghdr *nl;
	struct rtmsg *route_addr;
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
	nl->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	nl->nlmsg_type = RTM_DELROUTE;
	nl->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nl->nlmsg_seq = global_nl_seq++;
	nl->nlmsg_pid = local_nl.nl_pid;
	
	route_addr = (struct rtmsg *) NLMSG_DATA (nl);
	
	route_addr->rtm_family = AF_INET;
	route_addr->rtm_scope = RT_SCOPE_NOWHERE;
	route_addr->rtm_type = route->type;
	route_addr->rtm_table = route->table;
	route_addr->rtm_dst_len = route->prefix;
	route_addr->rtm_protocol = 0;
	
	if (route->prefix != 0) {
		rta = (struct rtattr *) RTM_RTA (route_addr);
		rta->rta_type = RTA_DST;
		rta->rta_len = RTA_LENGTH (sizeof (struct in_addr));
		memcpy (RTA_DATA(rta), &route->dest, sizeof (struct in_addr));
		nl->nlmsg_len += rta->rta_len;
	}
	
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
			printf ("Route DEL Msg type: DONE!\n");
			break;
		}
		if (nl->nlmsg_type == NLMSG_ERROR) {
			l_err = (struct nlmsgerr*) NLMSG_DATA (nl);
			if (nl->nlmsg_len < NLMSG_LENGTH (sizeof (struct nlmsgerr))) {
				printf ("Route DEL Error tamaño truncado\n");
			} else if (l_err->error != 0) {
				// Error:
				printf ("Route DEL Error: %i\n", l_err->error);
			}
			break;
		}
	}
}

void routes_add_or_update_rtm (NetworkInadorHandle *handle, struct nlmsghdr *h) {
	struct rtmsg *route_addr;
	struct rtattr *attribute;
	size_t len;
	char ip_as_string[1024];
	Routev4 *new;
	struct in_addr dest;
	int prefix;
	struct in_addr gateway;
	unsigned index = 0;
	
	memset (&dest, 0, sizeof (dest));
	memset (&gateway, 0, sizeof (gateway));
	
	route_addr = NLMSG_DATA(h);
	
	if (route_addr->rtm_type == RTN_BROADCAST || route_addr->rtm_type == RTN_LOCAL) {
		printf ("Omitiendo ruta local o broadcast\n");
		return;
	}
	
	if (route_addr->rtm_family != AF_INET) {
		/* Por el momento, las direcciones IPv6 no son procesadas */
		return;
	}
	
	printf ("--------\n");
	printf ("Route list, family: %d\n", route_addr->rtm_family);
	printf ("Route Dest len: %d\n", route_addr->rtm_dst_len);
	printf ("Route Table id: %d\n", route_addr->rtm_table);
	printf ("Route type: %d\n", route_addr->rtm_type);
	printf ("Route flags: %d\n", route_addr->rtm_flags);
	printf ("Route protocol: %d\n", route_addr->rtm_protocol);
	
	prefix = route_addr->rtm_dst_len;
	
	len = RTM_PAYLOAD (h);
	//len = h->nlmsg_len - NLMSG_LENGTH (sizeof (struct rtmsg));
	
	for_each_rattr (attribute, RTM_RTA (route_addr), len) {
		//printf ("Attribute (addr): %d\n", attribute->rta_type);
		
		if (attribute->rta_type == RTA_GATEWAY) {
			memcpy (&gateway, RTA_DATA (attribute), sizeof (struct in_addr));
			
			inet_ntop (route_addr->rtm_family, RTA_DATA (attribute), ip_as_string, sizeof (ip_as_string));
			printf ("Gw Address: %s\n", ip_as_string);
		} else if (attribute->rta_type == RTA_DST) {
			memcpy (&dest, RTA_DATA (attribute), sizeof (struct in_addr));
			
			inet_ntop (route_addr->rtm_family, RTA_DATA (attribute), ip_as_string, sizeof (ip_as_string));
			printf ("Destination Address: %s/%d\n", ip_as_string, route_addr->rtm_dst_len);
		} else if (attribute->rta_type == RTA_OIF) {
			index = *((int *) RTA_DATA (attribute));
			printf ("!!!!!!!!! Output interface: %i\n", index);
		} else if (attribute->rta_type == RTA_PREFSRC) {
			//printf (
		} else if (attribute->rta_type == RTA_TABLE) {
			
		} else {
			printf ("ROUTE Attribute: %d\n", attribute->rta_type);
		}
	}
	
	if (route_addr->rtm_type == RTN_BROADCAST || route_addr->rtm_type == RTN_LOCAL) {
		printf ("Omitiendo ruta local o broadcast\n");
		return;
	}
	
	new = _routes_search_ipv4 (handle, dest, prefix, index);
	
	if (new == NULL) {
		printf ("Agregando Ruta a la lista de rutas\n");
		new = _routes_append_routev4_to_struct (handle, dest, prefix);
	}
	
	/* Actualizar la gateway si hubo gateway en el mensaje */
	memcpy (&new->gateway, &gateway, sizeof (new->gateway));
	new->index = index;
	new->table = route_addr->rtm_table;
	new->type = route_addr->rtm_type;
}

void routes_del_rtm (NetworkInadorHandle *handle, struct nlmsghdr *h) {
	struct rtmsg *route_addr;
	Routev4 *route, *before;
	struct rtattr *attribute;
	size_t len;
	char ip_as_string[1024];
	struct in_addr dest;
	int prefix;
	int index;
	
	memset (&dest, 0, sizeof (dest));
	
	route_addr = NLMSG_DATA(h);
	
	if (route_addr->rtm_type == RTN_BROADCAST || route_addr->rtm_type == RTN_LOCAL) {
		//printf ("Omitiendo ruta local o broadcast\n");
		//return;
	}
	
	if (route_addr->rtm_family != AF_INET) {
		/* Por el momento, las direcciones IPv6 no son procesadas */
		return;
	}
	
	printf ("--------> ROUTE DEL <--------\n");
	printf ("Route list, family: %d\n", route_addr->rtm_family);
	printf ("Route Dest len: %d\n", route_addr->rtm_dst_len);
	printf ("Route Table id: %d\n", route_addr->rtm_table);
	printf ("Route type: %d\n", route_addr->rtm_type);
	printf ("Route flags: %d\n", route_addr->rtm_flags);
	printf ("Route protocol: %d\n", route_addr->rtm_protocol);
	
	prefix = route_addr->rtm_dst_len;
	
	//len = RTM_PAYLOAD (h);
	len = h->nlmsg_len - NLMSG_LENGTH (sizeof (struct rtmsg));
	
	for (attribute = RTM_RTA (route_addr); RTA_OK (attribute, len); attribute = RTA_NEXT (attribute, len)) {
		//printf ("Attribute (addr): %d\n", attribute->rta_type);
		
		if (attribute->rta_type == RTA_GATEWAY) {
			//memcpy (&gateway, RTA_DATA (attribute), sizeof (struct in_addr));
			
			inet_ntop (route_addr->rtm_family, RTA_DATA (attribute), ip_as_string, sizeof (ip_as_string));
			printf ("Gw Address: %s\n", ip_as_string);
		} else if (attribute->rta_type == RTA_DST) {
			memcpy (&dest, RTA_DATA (attribute), sizeof (struct in_addr));
			
			inet_ntop (route_addr->rtm_family, RTA_DATA (attribute), ip_as_string, sizeof (ip_as_string));
			printf ("Address: %s/%d\n", ip_as_string, route_addr->rtm_dst_len);
		} else if (attribute->rta_type == RTA_OIF) {
			index = *((int *) RTA_DATA (attribute));
			printf ("Output interface: %i\n", index);
		} else {
			printf ("ROUTE Attribute: %d\n", attribute->rta_type);
		}
	}
	
	route = _routes_search_ipv4 (handle, dest, prefix, index);
	
	if (route == NULL) {
		printf ("Me solicitaron eliminar una ruta y no existe\n");
	} else {
		if (route == handle->rtable_v4) {
			handle->rtable_v4 = route->next;
		} else {
			before = handle->rtable_v4;
			
			while (before->next != route) {
				before = before->next;
			}
			
			before->next = route->next;
		}
		
		_routes_delete_by_chain (handle, route);
		
		free (route);
	}
}

void routes_list (NetworkInadorHandle *handle, int sock) {
	struct msghdr rtnl_msg;    /* generic msghdr struct for use with sendmsg */
	struct iovec io;
	struct rtmsg *rt;
	struct sockaddr_nl kernel;
	char buffer[8192]; /* a large buffer */
	int len;
	
	/* Para la respuesta */
	struct nlmsghdr *msg_ptr;    /* pointer to current part */
	
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (sock, (struct sockaddr *) &local_nl, &local_size);
	
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	memset (&kernel, 0, sizeof (kernel));
	memset (&buffer, 0, sizeof (buffer));

	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	msg_ptr = (struct nlmsghdr *) buffer;
	msg_ptr->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	msg_ptr->nlmsg_type = RTM_GETROUTE;
	msg_ptr->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP; 
	msg_ptr->nlmsg_seq = global_nl_seq++;
	msg_ptr->nlmsg_pid = local_nl.nl_pid;
	
	rt = (struct rtmsg*) NLMSG_DATA (msg_ptr);
	rt->rtm_family = AF_INET; /* Limitar la primer consulta a solo IPv4, por el momento */

	io.iov_base = buffer;
	io.iov_len = msg_ptr->nlmsg_len;
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	sendmsg (sock, (struct msghdr *) &rtnl_msg, 0);
	
	/* Esperar la respuesta */
	memset (&io, 0, sizeof (io));
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	memset (buffer, 0, sizeof (buffer));
	
	io.iov_base = buffer;
	io.iov_len = sizeof (buffer);
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof (kernel);

	while ((len = recvmsg(sock, &rtnl_msg, 0)) > 0) { /* read lots of data */
		msg_ptr = (struct nlmsghdr *) buffer;
		if (msg_ptr->nlmsg_type == NLMSG_DONE) break;
	
		for (; NLMSG_OK(msg_ptr, len); msg_ptr = NLMSG_NEXT(msg_ptr, len)) {
			/* Procesar solo los mensajes de nueva interfaz */
			printf ("ROUTE Msg type: %i\n", msg_ptr->nlmsg_type);
			if (msg_ptr->nlmsg_type == RTM_NEWROUTE) {
				routes_add_or_update_rtm (handle, msg_ptr);
			}
		}
	}
}
