/*
 * wireless_bss.h
 * This file is part of Network-inador
 *
 * Copyright (C) 2020 - Félix Arreola Rodríguez
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

#ifndef __WIRELESS_BSS__
#define __WIRELESS_BSS__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#include <netlink/genl/ctrl.h>
#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/genl/genl.h>

#include <linux/nl80211.h>

#include "common.h"

int wireless_bss_parse_station_scan (struct nl_msg *msg, void *arg);
int wireless_bss_finish_scan (struct nl_msg *msg, void *arg);

#endif /* __WIRELESS_BSS__ */

