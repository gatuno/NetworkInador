/*
 * wireless_bss.c
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
#include "interfaces.h"
#include "netlink-events.h"
#include "wireless_if.h"
#include "wireless_bss.h"

#define DEFAULT_BSS_EXPIRATION_SCAN_COUNT 2

int wireless_bss_finish_scan (struct nl_msg *msg, void *arg) {
	#if 0
	GList *g;
	WirelessBSS *bss;
	printf ("Scan finish handler\n");
	
	g = 
	if (bss->last_update_idx < wpa_s->bss_update_idx)
			bss->scan_miss_count++;
	#endif
}

// Based on NetworkManager/src/platform/wifi/wifi-utils-nl80211.c
static void _wireless_bss_find_ssid (uint8_t *ies, uint32_t ies_len, uint8_t **ssid, uint32_t *ssid_len) {
#define WLAN_EID_SSID 0
	*ssid = NULL;
	*ssid_len = 0;

	while (ies_len > 2 && ies[0] != WLAN_EID_SSID) {
		ies_len -= ies[1] + 2;
		ies += ies[1] + 2;
	}
	if (ies_len < 2) return;
	if (ies_len < (uint32_t)(2 + ies[1])) return;

	*ssid_len = ies[1];
	*ssid = ies + 2;
}

static WirelessBSS * _wireless_bss_get (Interface *iface, const uint8_t *bssid, const uint8_t *ssid, int ssid_len) {
	GList *g;
	WirelessBSS *bss;
	if (iface->is_wireless == 0) return NULL;
	
	for (g = iface->wireless->aps; g != NULL; g = g->next) {
		bss = (WirelessBSS *) g->data;
		
		if (memcmp (bss->bssid, bssid, ETHER_ADDR_LEN) == 0 &&
		    bss->ssid_len == ssid_len &&
		    memcmp (bss->ssid, ssid, ssid_len) == 0) {
			return bss;
		}
	}
	
	return NULL;
}

static WirelessBSS * _wireless_bss_add_bss (Interface *iface, WirelessBSS *bss) {
	WirelessBSS *new;
	
	new = (WirelessBSS *) malloc (sizeof (WirelessBSS));
	
	if (new == NULL) return NULL;
	
	memcpy (new, bss, sizeof (WirelessBSS));
	
	new->last_update_idx = iface->wireless->bss_update_idx;
	new->scan_miss_count = 0;
	
	iface->wireless->aps = g_list_append (iface->wireless->aps, new);
}

static void _wireless_bss_update_bss (Interface *iface, WirelessBSS *bss, WirelessBSS *updated) {
	bss->last_update_idx = iface->wireless->bss_update_idx;
	bss->scan_miss_count = 0;
	
	/* Actualizar este bss con los nuevos datos */
	bss->freq = updated->freq;
}

int wireless_bss_parse_station_scan (struct nl_msg *msg, void *arg) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) arg;
	struct nlmsgerr *l_err;
	struct nlmsghdr *reply;
	struct genlmsghdr *gnlh;
	int remaining, remaining2;
	struct nlattr *attr, *bss_attr;
	Interface *iface = NULL;
	
	WirelessBSS bss, *search_bss;
	
	reply = nlmsg_hdr (msg);
	
	if (reply->nlmsg_type == NLMSG_ERROR) {
		l_err = nlmsg_data (reply);
		
		return NL_SKIP;
	}
	
	printf ("Estación recibida por escaneo\n");
	gnlh = nlmsg_data (reply);
	memset (&bss, 0, sizeof (bss));
	
	nlmsg_for_each_attr(attr, reply, sizeof (struct genlmsghdr), remaining) {
		//printf ("Estacion nueva. Attr = %i\n", nla_type (attr));
		if (nla_type (attr) == NL80211_ATTR_IFINDEX) {
			iface = _interfaces_locate_by_index (handle->interfaces, nla_get_u32 (attr));
			//printf ("Estación nueva en la interfaz %i\n", nla_get_u32 (attr));
		} else if (nla_type (attr) == NL80211_ATTR_BSS) {
			//printf ("Información BSS:\n");
			
			remaining2 = nla_len (attr);
			bss_attr = nla_data (attr);
			nla_for_each_nested(bss_attr, attr, remaining2) {
				printf ("-> BSS: %i\n", nla_type (bss_attr));
				if (nla_type (bss_attr) == NL80211_BSS_BSSID) {
					char *mac = nla_data (bss_attr);
					printf ("MAC access point: %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
					memcpy (bss.bssid, nla_data (bss_attr), ETHER_ADDR_LEN);
				} else if (nla_type (bss_attr) == NL80211_BSS_FREQUENCY) {
					printf ("Freq: %i\n", nla_get_u32 (bss_attr));
					bss.freq = nla_get_u32 (bss_attr);
				} else if (nla_type (bss_attr) == NL80211_BSS_INFORMATION_ELEMENTS) {
					char *ssid;
					int ssid_len;
					_wireless_bss_find_ssid (nla_data (bss_attr), nla_len (bss_attr), (uint8_t **) &ssid, &ssid_len);
					
					if (ssid != NULL) {
						printf ("Essid: «%.*s»\n", ssid_len, ssid);
						memcpy (bss.ssid, ssid, ssid_len);
					}
				}
			}
		}
	}
	
	if (iface == NULL) return NL_SKIP;
	
	search_bss = _wireless_bss_get (iface, bss.bssid, bss.ssid, bss.ssid_len);
	
	if (search_bss == NULL) {
		/* Agregar como nuevo */
		search_bss = _wireless_bss_add_bss (iface, &bss);
	} else {
		/* Actualizar este bss */
		_wireless_bss_update_bss (iface, search_bss, &bss);
	}
	
	return NL_SKIP;
}

