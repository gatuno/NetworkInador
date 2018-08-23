/*
 * manager-events.h
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

#ifndef __MANAGER_EVENTS_H__
#define __MANAGER_EVENTS_H__

#include "network-inador.h"

enum {
	MANAGER_EVENT_IPV4_ADDED = 1,
	MANAGER_EVENT_IPV4_REMOVED,
};

void manager_events_notify_ipv4_address_added (Interface *iface, IPv4 *address);
void manager_events_setup (NetworkInadorHandle *handle);

#endif

