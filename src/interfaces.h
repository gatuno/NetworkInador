/*
 * interfaces.h
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

#ifndef __INTERFACES_H__
#define __INTERFACES_H__

#include <netlink/socket.h>
#include <netlink/msg.h>

#include "common.h"

void interfaces_init (NetworkInadorHandle *handle);
int interface_receive_message_newlink (struct nl_msg *msg, void *arg);
int interface_receive_message_dellink (struct nl_msg *msg, void *arg);

Interface * _interfaces_locate_by_index (GList *list, int index);

int interfaces_change_mac_address (NetworkInadorHandle *handle, int index, void *new_mac);
int interfaces_change_mtu (NetworkInadorHandle *handle, int index, uint32_t new_mtu);
int interfaces_change_set_up (NetworkInadorHandle *handle, int index);
int interfaces_change_set_down (NetworkInadorHandle *handle, int index);
int interfaces_change_name (NetworkInadorHandle *handle, int index, char * new_name);

#endif

