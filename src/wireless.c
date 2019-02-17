/*
 * wireless.c
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

#include <linux/genetlink.h>

#include <linux/nl80211.h>

#include "wireless.h"
#include "network-inador.h"
#include "interfaces.h"
#include "rta_aux.h"

static uint16_t nl80211_id = 0;
static uint16_t scan_multicast_group_id = 0;

static void _wireless_find_ssid (uint8_t *ies, uint32_t ies_len, unsigned char *ssid, uint32_t *ssid_len) {
#define WLAN_EID_SSID 0
	ssid[0] = 0;
	*ssid_len = 0;

	while (ies_len > 2 && ies[0] != WLAN_EID_SSID) {
		ies_len -= ies[1] + 2;
		ies += ies[1] + 2;
	}
	if (ies_len < 2)
		return;
	if (ies_len < (uint32_t)(2 + ies[1]))
		return;

	*ssid_len = ies[1];
	memcpy (ssid, ies + 2, ies[1]);
}

static int _wireless_create_generic_netlink_socket (void) {
	int fd;
	struct sockaddr_nl local;
	
	fd = socket (AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	
	if (fd < 0) {
		return -1;
	}
	
	memset(&local, 0, sizeof(local)); /* fill-in local address information */
	local.nl_family = AF_NETLINK;
	local.nl_pid = 0;
	local.nl_groups = 0;
	
	if (bind (fd, (struct sockaddr *) &local, sizeof(local)) < 0) {
		perror("cannot bind, are you root ? if yes, check netlink/rtnetlink kernel support");
		close (fd);
		return -1;
	}
	
	return fd;
}

static uint16_t _wireless_parse_family_id (struct nlmsghdr *msg_ptr) {
	struct genlmsghdr *ghdr;
	struct rtattr *attribute;
	int len;
	
	uint16_t n80211_family;
	
	struct rtattr *nest2_attr, *nest3_attr;
	int nest2_size, nest3_size;
	int sub2_len, sub3_len;
	
	ghdr = NLMSG_DATA (msg_ptr);
	len = msg_ptr->nlmsg_len - NLMSG_LENGTH (sizeof (struct genlmsghdr));
	
	attribute = (struct rtattr*) (((char *) ghdr) + GENL_HDRLEN);
	
	for (; RTA_OK (attribute, len); attribute = RTA_NEXT (attribute, len)) {
		//printf ("Attribute: %d\n", attribute->rta_type);
		if (attribute->rta_type == CTRL_ATTR_FAMILY_ID) {
			memcpy (&n80211_family, RTA_DATA (attribute), 2);
			//printf ("El id del nl80211 es: %hu\n", n80211_family);
		} else if (attribute->rta_type == CTRL_ATTR_MCAST_GROUPS) {
			//printf ("_____________ Descubriendo la familia nl80211. Multicast groups: \n");
			int id_multicast_group;
			char multicast_group_name[512];
			
			nest2_size = attribute->rta_len;
			nest2_attr = RTA_DATA (attribute);
			
			while (nest2_size > sizeof (nest2_attr)) {
				sub2_len = nest2_attr->rta_len;
				if (sub2_len > nest2_size) {
					break;
				}
				
				nest3_size = nest2_attr->rta_len;
				nest3_attr = RTA_DATA (nest2_attr);
				
				id_multicast_group = 0;
				multicast_group_name[0] = 0;
				
				while (nest3_size > sizeof (nest3_attr)) {
					sub3_len = nest3_attr->rta_len;
					if (sub3_len > nest3_size) {
						//printf ("Los sub atributos se acabaron prematuramente\n");
						break;
					}
					//printf ("sub attributo type: %i, size: %d\n", nest3_attr->rta_type, nest3_attr->rta_len);
					if (nest3_attr->rta_type == CTRL_ATTR_MCAST_GRP_ID) {
						memcpy (&id_multicast_group, RTA_DATA (nest3_attr), sizeof (id_multicast_group));
					} else if (nest3_attr->rta_type == CTRL_ATTR_MCAST_GRP_NAME) {
						strncpy (multicast_group_name, RTA_DATA (nest3_attr), sizeof (multicast_group_name));
					}
					
					nest3_size -= RTA_ALIGN (sub3_len);
					nest3_attr = (struct rtattr *) (((char *) nest3_attr) + RTA_ALIGN (sub3_len));
				}
				
				if (strcmp (multicast_group_name, "scan") == 0) {
					scan_multicast_group_id = id_multicast_group;
					//printf ("ID del grupo multicast SCAN: %i\n", id_multicast_group);
				}
				
				nest2_size -= RTA_ALIGN (sub2_len);
				nest2_attr = (struct rtattr *) (((char *) nest2_attr) + RTA_ALIGN (sub2_len));
			}
		}
	}
	
	return n80211_family;
}

static void _wireless_parse_message_get_iface (struct nlmsghdr *msg_ptr, Interface *iface) {
	struct genlmsghdr *ghdr;
	struct rtattr *attribute;
	int len;
	int wiphy;
	
	/* Anexar la estructura wireless al objeto interfaz */
	WirelessInfo *winfo;
	
	winfo = (WirelessInfo *) malloc (sizeof (WirelessInfo));
	
	if (winfo == NULL) {
		return;
	}
	
	memset (winfo, 0, sizeof (winfo));
	
	iface->wireless = winfo;
	
	ghdr = NLMSG_DATA (msg_ptr);
	len = msg_ptr->nlmsg_len - NLMSG_LENGTH (sizeof (struct genlmsghdr));
	
	attribute = (struct rtattr*) (((char *) ghdr) + GENL_HDRLEN);
	
	for (; RTA_OK (attribute, len); attribute = RTA_NEXT (attribute, len)) {
		printf ("Wireless Attribute: %d\n", attribute->rta_type);
		if (attribute->rta_type == NL80211_ATTR_WIPHY) {
			memcpy (&wiphy, RTA_DATA (attribute), 4);
			iface->wireless->wiphy = wiphy;
		}
		/*switch(attribute->rta_type) {
			default:
				printf ("Del interface RTA Attribute \"%hu\" no procesado\n", attribute->rta_type);
		}*/
	}
}

static void _wireless_parse_scan (NetworkInadorHandle *handle, struct nlmsghdr *msg_ptr) {
	struct genlmsghdr *ghdr;
	struct rtattr *attribute;
	int len;
	int iface = -1;
	
	ghdr = NLMSG_DATA (msg_ptr);
	len = msg_ptr->nlmsg_len - NLMSG_LENGTH (sizeof (struct genlmsghdr));
	
	
	attribute = (struct rtattr*) (((char *) ghdr) + GENL_HDRLEN);
	
	for (; RTA_OK (attribute, len); attribute = RTA_NEXT (attribute, len)) {
		//printf ("Wireless scan Attribute: %d\n", attribute->rta_type);
		if (attribute->rta_type == NL80211_ATTR_IFINDEX) {
			memcpy (&iface, RTA_DATA (attribute), 4);
		}
	}
	
	/* Ejecutar el GET Scan */
	if (iface != -1) {
		wireless_do_get_scan (handle, iface);
	}
}

static void _wireless_parse_station (struct nlmsghdr *msg_ptr, Interface *iface) {
	struct genlmsghdr *ghdr;
	struct rtattr *attribute;
	int len;
	char bss_info[1024];
	int has_bss = 0;
	int bss_len;
	
	ghdr = NLMSG_DATA (msg_ptr);
	len = msg_ptr->nlmsg_len - NLMSG_LENGTH (sizeof (struct genlmsghdr));
	
	attribute = (struct rtattr*) (((char *) ghdr) + GENL_HDRLEN);
	
	for (; RTA_OK (attribute, len); attribute = RTA_NEXT (attribute, len)) {
		//printf ("Wireless scan Attribute: %d\n", attribute->rta_type);
		if (attribute->rta_type == NL80211_ATTR_BSS) {
			has_bss = 1;
			bss_len = attribute->rta_len;
			
			memcpy (bss_info, RTA_DATA (attribute), bss_len);
		}
	}
	
	if (has_bss == 0) {
		/* No hay BSS info */
		return;
	}
	
	printf ("Imprimiendo BSS Attrs:\n");
	attribute = (struct rtattr *) bss_info;
	for (; RTA_OK (attribute, bss_len); attribute = RTA_NEXT (attribute, len)) {
		
		if (attribute->rta_type == NL80211_BSS_BSSID) {
			char *mac = RTA_DATA (attribute);
			//printf ("MAC access point: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		} else if (attribute->rta_type == NL80211_BSS_INFORMATION_ELEMENTS) {
			char essid[64];
			essid[0] = 0;
			int essid_len = 0;
			_wireless_find_ssid (RTA_DATA (attribute), attribute->rta_len, essid, &essid_len);
			essid[essid_len] = 0;
			printf ("Essid: %s\n", essid);
		} else if (attribute->rta_type == NL80211_BSS_STATUS) {
			int status;
			memcpy (&status, RTA_DATA (attribute), 4);
			printf ("-> Bss estatus: %i\n", status);
		} else {
			printf ("BSS Scan Attribute: %d\n", attribute->rta_type);
		}
	}
}

void wireless_do_get_scan (NetworkInadorHandle *handle, int ifindex) {
	printf ("DO Scan for iface: %i\n", ifindex);
	struct msghdr rtnl_msg;    /* generic msghdr struct for use with sendmsg */
	struct iovec io;
	struct genlmsghdr *ghdr;
	struct rtattr *rta;
	struct sockaddr_nl kernel;
	char buffer[8192]; /* a large buffer */
	int len;
	Interface *iface;
	
	/* Localizar el iface por el índice */
	iface = interfaces_locate_by_index (handle->interfaces, ifindex);
	
	if (iface == NULL) {
		return;
	}
	
	/* Para la respuesta */
	struct nlmsgerr *l_err;
	struct nlmsghdr *msg_ptr;    /* pointer to current part */
	
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (handle->netlink_sock_request_generic, (struct sockaddr *) &local_nl, &local_size);
	
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	memset (&kernel, 0, sizeof (kernel));
	memset (buffer, 0, sizeof (buffer));

	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	msg_ptr = (struct nlmsghdr *) buffer;
	msg_ptr->nlmsg_len = NLMSG_LENGTH (sizeof (struct genlmsghdr));
	msg_ptr->nlmsg_type = nl80211_id;
	msg_ptr->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP;
	msg_ptr->nlmsg_seq = global_nl_seq++;
	msg_ptr->nlmsg_pid = local_nl.nl_pid;
	
	ghdr = (struct genlmsghdr *) NLMSG_DATA (msg_ptr);
	ghdr->cmd = NL80211_CMD_GET_SCAN;
	ghdr->version = 0;
	
	rta_addattr_l (msg_ptr, sizeof (buffer), NL80211_ATTR_IFINDEX, &ifindex, 4);
	//rta_addattr_l (msg_ptr, sizeof (buffer), NL80211_ATTR_IFINDEX, &iface->wiphy, 4);
	
	io.iov_base = buffer;
	io.iov_len = msg_ptr->nlmsg_len;
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	len = sendmsg (handle->netlink_sock_request_generic, (struct msghdr *) &rtnl_msg, 0);
	
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
	
	len = recvmsg (handle->netlink_sock_request_generic, &rtnl_msg, 0);
	
	if (len <= 0) {
		return;
	}
	
	msg_ptr = (struct nlmsghdr *) buffer;
	printf ("Procesando mensajes de solicitud de escaneo\n");
	for (; NLMSG_OK(msg_ptr, len); msg_ptr = NLMSG_NEXT(msg_ptr, len)) {
		if (msg_ptr->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *l_err;
			l_err = (struct nlmsgerr*) NLMSG_DATA (msg_ptr);
			printf ("DO Scan message error, num: %i\n", l_err->error);
			break;
		} else if (msg_ptr->nlmsg_type == NLMSG_DONE) {
			printf ("DO Scan, end of DUMP\n");
			break;
		} else if (msg_ptr->nlmsg_type == nl80211_id) {
			ghdr = NLMSG_DATA (msg_ptr);
			
			//printf ("DO SCAN! Generic command: %i\n", ghdr->cmd);
			if (ghdr->cmd == NL80211_CMD_NEW_SCAN_RESULTS) {
				_wireless_parse_station (msg_ptr, iface);
			}
		}
	}
}

gboolean _wireless_events_handle_read (GIOChannel *source, GIOCondition condition, gpointer data) {
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
	struct genlmsghdr *ghdr;
	
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
		if (msg_ptr->nlmsg_type == nl80211_id) {
			ghdr = NLMSG_DATA (msg_ptr);
			
			printf ("Generic command: %i\n", ghdr->cmd);
			if (ghdr->cmd == NL80211_CMD_NEW_SCAN_RESULTS) {
				_wireless_parse_scan (handle, msg_ptr);
			}
		}
	}
	
	return TRUE;
}

void wireless_init (NetworkInadorHandle *handle) {
	struct msghdr rtnl_msg;    /* generic msghdr struct for use with sendmsg */
	struct iovec io;
	struct genlmsghdr *ghdr;
	struct rtattr *rta;
	struct sockaddr_nl kernel;
	char buffer[8192]; /* a large buffer */
	int len;
	GIOChannel *channel;
	
	/* Para la respuesta */
	struct nlmsgerr *l_err;
	struct nlmsghdr *msg_ptr;    /* pointer to current part */
	
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Crear un netlink de la familia generica para nuestras peticiones */
	handle->netlink_sock_request_generic = _wireless_create_generic_netlink_socket ();
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (handle->netlink_sock_request_generic, (struct sockaddr *) &local_nl, &local_size);
	
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	memset (&kernel, 0, sizeof (kernel));
	memset (buffer, 0, sizeof (buffer));

	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	msg_ptr = (struct nlmsghdr *) buffer;
	msg_ptr->nlmsg_len = NLMSG_LENGTH (sizeof (struct genlmsghdr));
	msg_ptr->nlmsg_type = GENL_ID_CTRL;
	msg_ptr->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	msg_ptr->nlmsg_seq = global_nl_seq++;
	msg_ptr->nlmsg_pid = local_nl.nl_pid;
	
	ghdr = (struct genlmsghdr *) NLMSG_DATA (msg_ptr);
	ghdr->cmd = CTRL_CMD_GETFAMILY;
	ghdr->version = 1;
	
	len = strlen (NL80211_GENL_NAME) + 1;
	rta_addattr_l (msg_ptr, sizeof (buffer), CTRL_ATTR_FAMILY_NAME, NL80211_GENL_NAME, len);
	
	io.iov_base = buffer;
	io.iov_len = msg_ptr->nlmsg_len;
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	sendmsg (handle->netlink_sock_request_generic, (struct msghdr *) &rtnl_msg, 0);
	
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

	/*len = recvmsg (handle->netlink_sock_request_generic, &rtnl_msg, 0);
	msg_ptr = (struct nlmsghdr *) buffer;
	_wireless_parse_family_id (msg_ptr);
	
	len = recvmsg (handle->netlink_sock_request_generic, &rtnl_msg, 0);
	msg_ptr = (struct nlmsghdr *) buffer;*/
	while ((len = recvmsg(handle->netlink_sock_request_generic, &rtnl_msg, 0)) > 0) { /* read lots of data */
		msg_ptr = (struct nlmsghdr *) buffer;
		if (msg_ptr->nlmsg_type == NLMSG_DONE) break;
		if (msg_ptr->nlmsg_type == NLMSG_ERROR) break;
		
		if (msg_ptr->nlmsg_type == GENL_ID_CTRL) {
			nl80211_id = _wireless_parse_family_id (msg_ptr);
		}
	}
	
	/* Generar otro socket netlink generic para eventos */
	int sock_for_wireless_events;
	int group;
	
	sock_for_wireless_events = _wireless_create_generic_netlink_socket ();
	
	group = scan_multicast_group_id;
	setsockopt (sock_for_wireless_events, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &group, sizeof (group));
	
	/* Instalar un GIOChannel */
	channel = g_io_channel_unix_new (sock_for_wireless_events);
	
	g_io_add_watch (channel, G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP, _wireless_events_handle_read, handle);
}

//void test_wireless
void wireless_check_is_wireless_interface (NetworkInadorHandle *handle, Interface *iface) {
	struct msghdr rtnl_msg;    /* generic msghdr struct for use with sendmsg */
	struct iovec io;
	struct genlmsghdr *ghdr;
	struct rtattr *rta;
	struct sockaddr_nl kernel;
	char buffer[8192]; /* a large buffer */
	int len;
	
	/* Para la respuesta */
	struct nlmsgerr *l_err;
	struct nlmsghdr *msg_ptr;    /* pointer to current part */
	
	struct sockaddr_nl local_nl;
	socklen_t local_size;
	
	/* Recuperar el puerto local del netlink */
	local_size = sizeof (local_nl);
	getsockname (handle->netlink_sock_request_generic, (struct sockaddr *) &local_nl, &local_size);
	
	memset (&rtnl_msg, 0, sizeof (rtnl_msg));
	memset (&kernel, 0, sizeof (kernel));
	memset (buffer, 0, sizeof (buffer));

	kernel.nl_family = AF_NETLINK; /* fill-in kernel address (destination of our message) */
	kernel.nl_groups = 0;
	
	msg_ptr = (struct nlmsghdr *) buffer;
	msg_ptr->nlmsg_len = NLMSG_LENGTH (sizeof (struct genlmsghdr));
	msg_ptr->nlmsg_type = nl80211_id;
	msg_ptr->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	msg_ptr->nlmsg_seq = global_nl_seq++;
	msg_ptr->nlmsg_pid = local_nl.nl_pid;
	
	ghdr = (struct genlmsghdr *) NLMSG_DATA (msg_ptr);
	ghdr->cmd = NL80211_CMD_GET_INTERFACE;
	ghdr->version = 0;
	
	rta_addattr_l (msg_ptr, sizeof (buffer), NL80211_ATTR_IFINDEX, &iface->index, 4);
	
	io.iov_base = buffer;
	io.iov_len = msg_ptr->nlmsg_len;
	rtnl_msg.msg_iov = &io;
	rtnl_msg.msg_iovlen = 1;
	rtnl_msg.msg_name = &kernel;
	rtnl_msg.msg_namelen = sizeof(kernel);
	
	sendmsg (handle->netlink_sock_request_generic, (struct msghdr *) &rtnl_msg, 0);
	
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
	
	while ((len = recvmsg(handle->netlink_sock_request_generic, &rtnl_msg, 0)) > 0) { /* read lots of data */
		msg_ptr = (struct nlmsghdr *) buffer;
		if (msg_ptr->nlmsg_type == NLMSG_DONE) break;
		if (msg_ptr->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *l_err;
			l_err = (struct nlmsgerr*) NLMSG_DATA (msg_ptr);
			break;
		}
		
		if (msg_ptr->nlmsg_type == nl80211_id) {
			_wireless_parse_message_get_iface (msg_ptr, iface);
		}
	}
}

