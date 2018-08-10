/*
 * rta_aux.c
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

#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if.h>
#include <linux/if_addr.h>

#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define NLMSG_TAIL(nmsg) \
	((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

int rta_addattr_l (struct nlmsghdr *n, int maxlen, int type, const void *data, int alen) {
	int len = RTA_LENGTH (alen);
	struct rtattr *rta;
	
	if (NLMSG_ALIGN (n->nlmsg_len) + RTA_ALIGN (len) > maxlen) {
		return -1;
	}
	
	rta = NLMSG_TAIL (n);
	rta->rta_type = type;
	rta->rta_len = len;
	memcpy (RTA_DATA (rta), data, alen);
	n->nlmsg_len = NLMSG_ALIGN (n->nlmsg_len) + RTA_ALIGN (len);
	
	return 0;
}

struct rtattr * rta_addattr_nest (struct nlmsghdr *n, int maxlen, int type) {
	struct rtattr *nest = NLMSG_TAIL (n);
	
	rta_addattr_l (n, maxlen, type, NULL, 0);
	
	return nest;
}

int rta_addattr_nest_end (struct nlmsghdr *n, struct rtattr *nest) {
	nest->rta_len = (void *) NLMSG_TAIL (n) - (void *) nest;
	return n->nlmsg_len;
}

