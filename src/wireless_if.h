/*
 * wireless_if.h
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

#ifndef __WIRELESS_IF_H__
#define __WIRELESS_IF_H__

#include "common.h"

typedef enum { /*< flags >*/
	WIFI_DEVICE_CAP_NONE          = 0x00000000,
	WIFI_DEVICE_CAP_CIPHER_WEP40  = 0x00000001,
	WIFI_DEVICE_CAP_CIPHER_WEP104 = 0x00000002,
	WIFI_DEVICE_CAP_CIPHER_TKIP   = 0x00000004,
	WIFI_DEVICE_CAP_CIPHER_CCMP   = 0x00000008,
	WIFI_DEVICE_CAP_WPA           = 0x00000010,
	WIFI_DEVICE_CAP_RSN           = 0x00000020,
	WIFI_DEVICE_CAP_AP            = 0x00000040,
	WIFI_DEVICE_CAP_ADHOC         = 0x00000080,
	WIFI_DEVICE_CAP_FREQ_VALID    = 0x00000100,
	WIFI_DEVICE_CAP_FREQ_2GHZ     = 0x00000200,
	WIFI_DEVICE_CAP_FREQ_5GHZ     = 0x00000400,
} DeviceWifiCapabilities;

void wireless_interface_check (NetworkInadorHandle *handle, Interface *iface);
int wireless_events_dispatcher (struct nl_msg *msg, void *arg);

extern uint16_t wireless_if_nl80211_id;

#endif /* __WIRELESS_IF_H__ */
