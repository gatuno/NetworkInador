/*
 * interfaces.h
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

#ifndef __INTERFACES_H__
#define __INTERFACES_H__

#include "network-inador.h"

extern int global_nl_seq;

void interfaces_list_all (NetworkInadorHandle *handle, int sock);
void interfaces_add_or_update_rtnl_link (NetworkInadorHandle *handle, struct nlmsghdr *h);
void interfaces_del_rtnl_link (NetworkInadorHandle *handle, struct nlmsghdr *h);
void interfaces_add_or_update_ipv4 (NetworkInadorHandle *handle, struct nlmsghdr *h);
void interfaces_del_ipv4 (NetworkInadorHandle *handle, struct nlmsghdr *h);

#endif

