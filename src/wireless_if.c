/*
 * wireless_if.c
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

uint16_t wireless_if_nl80211_id = 0;
static uint16_t wireless_if_nl80211_scan_multicast_group_id = 0;

int wireless_interface_new_scan_results (NetworkInadorHandle *handle, struct nl_msg *msg);
void wireless_interface_get_scan (NetworkInadorHandle *handle, Interface *iface);
void wireless_interface_trigger_scan (NetworkInadorHandle *handle, Interface *iface);

struct _wireless_iface_is_wifi {
	int error;
	int is_wifi;
	int index;
	WirelessInfo *info;
};

static void _wireless_if_process_bands (struct nlattr *list_bands, WirelessInfo *info) {
	int remaining, remaining2, remaining3, remaining4;
	struct nlattr *band, *band_attr, *freq, *freq_attr;
	int freq_index;
	
	printf ("%s: El info es: %p\n", __func__, info);
	
	info->num_freqs = 0;
	printf ("La lista de bandas tiene por tamaño: %i\n", nla_len (list_bands));
	nla_for_each_nested (band, list_bands, remaining) {
		
		nla_for_each_nested (band_attr, band, remaining2) {
			if (nla_type (band_attr) != NL80211_BAND_ATTR_FREQS) continue;
			nla_for_each_nested (freq, band_attr, remaining3) {
				printf ("Dentro de BAND attr freqs, atributo es: %i (%i)\n", nla_type (freq), nla_len (freq));
				nla_for_each_nested (freq_attr, freq, remaining4) {
					printf ("Dentro de FREQ attr, atributo es: %i (%i)\n", nla_type (freq_attr), nla_len (freq_attr));
					if (nla_type (freq_attr) != NL80211_FREQUENCY_ATTR_FREQ) continue;
					info->num_freqs++;
				}
			}
		}
	}
	
	printf ("La supuesta cantidad de frecuencias es: %i\n", info->num_freqs);
	info->freqs = (guint32 *) malloc (sizeof (guint32) * info->num_freqs);
	freq_index = 0;
	nla_for_each_nested (band, list_bands, remaining) {
		
		nla_for_each_nested (band_attr, band, remaining2) {
			if (nla_type (band_attr) != NL80211_BAND_ATTR_FREQS) continue;
			nla_for_each_nested (freq, band_attr, remaining3) {
				nla_for_each_nested (freq_attr, freq, remaining4) {
					if (nla_type (freq_attr) != NL80211_FREQUENCY_ATTR_FREQ) continue;
					
					info->freqs[freq_index] = nla_get_u32 (freq_attr);
					
					info->caps |= WIFI_DEVICE_CAP_FREQ_VALID;

					if (info->freqs[freq_index] > 2400 && info->freqs[freq_index] < 2500) {
						info->caps |= WIFI_DEVICE_CAP_FREQ_2GHZ;
					}
					if (info->freqs[freq_index] > 4900 && info->freqs[freq_index] < 6000) {
						info->caps |= WIFI_DEVICE_CAP_FREQ_5GHZ;
					}
					
					printf ("Frecuencia: %i\n", info->freqs[freq_index]);
					freq_index++;
				}
			}
		}
	}
}

static int _wireless_if_cb_valid_is_wifi (struct nl_msg *msg, void *arg) {
	struct _wireless_iface_is_wifi *is_wifi = (struct _wireless_iface_is_wifi *) arg;
	
	printf ("%s: Argumento extra: %p\n", __func__, arg);
	
	struct nlmsgerr *l_err;
	struct nlmsghdr *reply;
	struct genlmsghdr *gnlh;
	struct nlattr *attr, *nest_attr;
	int remaining, remaining2;
	
	reply = nlmsg_hdr (msg);
	printf ("CB Valid is wifi\n");
	if (reply->nlmsg_type == NLMSG_ERROR) {
		l_err = nlmsg_data (reply);
		
		is_wifi->error = l_err->error;
		printf ("---> (type = %i, NLMSG_ERROR = %i) Error %i", reply->nlmsg_type, NLMSG_ERROR, is_wifi->error);
		return NL_SKIP;
	}
	
	if (reply->nlmsg_type != wireless_if_nl80211_id) {
		return NL_SKIP;
	}
	gnlh = nlmsg_data (reply);
	
	printf ("---> type = nl80211_id, ");
	printf ("CMD: %i\n", gnlh->cmd);
	
	if (gnlh->cmd != NL80211_CMD_NEW_WIPHY) {
		/* Ignorar */
		return NL_SKIP;
	}
	
	/* Como está la información de la interfaz, es una interfaz wifi */
	is_wifi->is_wifi = 1;
	
	nlmsg_for_each_attr(attr, reply, sizeof (struct genlmsghdr), remaining) {
		printf ("Atributo de la interfaz wifi: %i\n", nla_type (attr));
		switch (nla_type (attr)) {
			case NL80211_ATTR_WIPHY:
				is_wifi->info->phy = nla_get_u32 (attr);
				break;
			case NL80211_ATTR_SUPPORTED_COMMANDS:
				nla_for_each_nested (nest_attr, attr, remaining2) {
					switch (nla_get_u32 (nest_attr)) {
						case NL80211_CMD_TRIGGER_SCAN:
							is_wifi->info->can_scan = TRUE;
							break;
						case NL80211_CMD_CONNECT:
						case NL80211_CMD_AUTHENTICATE:
							/* Only devices that support CONNECT or AUTH actually support
							 * 802.11, unlike say ipw2x00 (up to at least kernel 3.4) which
							 * has minimal info support, but no actual command support.
							 * This check mirrors what wpa_supplicant does to determine
							 * whether or not to use the nl80211 driver.
							 */
							is_wifi->info->supported = TRUE;
							break;
						default:
							break;
					}
				}
				break;
			case NL80211_ATTR_WIPHY_BANDS:
				printf ("Hay attr wiphy bands\n");
				_wireless_if_process_bands (attr, is_wifi->info);
				break;
		}
	}
	
	return NL_SKIP;
}

static int _wireless_if_wait_ack_or_error (struct nl_msg *msg, void *arg) {
	int *ret = (int *) arg;
	struct nlmsgerr *l_err;
	struct nlmsghdr *reply;
	
	reply = nlmsg_hdr (msg);
	printf ("--> _wireless_if_wait_ack_or_error");
	if (reply->nlmsg_type == NLMSG_ERROR) {
		l_err = nlmsg_data (reply);
		
		*ret = l_err->error;
		printf ("---> (type = %i, NLMSG_ERROR = %i) Error %i", reply->nlmsg_type, NLMSG_ERROR, *ret);
	} else {
		printf ("---> type = %i (nl802 = %i).", reply->nlmsg_type, wireless_if_nl80211_id);
	}
	printf ("\n");
	return NL_SKIP;
}

static int _wireless_if_wait_error (struct sockaddr_nl *nla, struct nlmsgerr *l_err, void *arg) {
	int *ret = (int *) arg;
	
	*ret = l_err->error;
	printf ("--> _wireless_if_wait_error\n");
	return NL_SKIP;
}

static int _wireless_if_valid (struct nl_msg *msg, void *arg) {
	printf ("CB Valid\n");
	return _wireless_if_wait_ack_or_error (msg, arg);
}

static int _wireless_if_invalid (struct nl_msg *msg, void *arg) {
	printf ("CB INValid\n");
	return _wireless_if_wait_ack_or_error (msg, arg);
}

static int _wireless_if_ack (struct nl_msg *msg, void *arg) {
	printf ("CB ACK\n");
	return _wireless_if_wait_ack_or_error (msg, arg);
}

#if 0
gboolean wireless_interface_timer_trigger (gpointer data) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) data;
	Interface *iface;
	
	static int c = 0;
	
	printf ("Ejecutando Trigger SCAN\n");
	
	iface = _interfaces_locate_by_index (handle->interfaces, 3);
	if (iface == NULL) return FALSE;
	wireless_interface_trigger_scan (handle, iface);
	
	if (c == 0) {
		c++;
		return TRUE;
	}
	return FALSE;
}
#endif

static void wireless_interface_init_nl80211 (NetworkInadorHandle *handle) {
	int family_id;
	int mcid;
	struct nl_sock * sock_req;
	
	sock_req = nl_socket_alloc ();
	
	if (nl_connect (sock_req, NETLINK_GENERIC) != 0) {
		perror ("Falló conectar netlink socket GENERIC\n");
		
		return;
	}
	
	family_id = genl_ctrl_resolve (sock_req, "nl80211");
	
	if (family_id < 0) {
		nl_socket_free (sock_req);
		return;
	}
	
	mcid = genl_ctrl_resolve_grp (sock_req, "nl80211", "scan");
	
	if (mcid >= 0) {
		/* Solo si tenemos el id del grupo multicast crear el generic de eventos */
		netlink_events_create_pair (&handle->nl80211_scan, NETLINK_GENERIC);
		if (handle->nl80211_scan.nl_sock != NULL) {
			nl_socket_add_membership (handle->nl80211_scan.nl_sock, mcid);
			nl_socket_modify_cb (handle->nl80211_scan.nl_sock, NL_CB_VALID, NL_CB_CUSTOM, wireless_events_dispatcher, handle);
		}
		wireless_if_nl80211_scan_multicast_group_id = mcid;
	}
	
	netlink_events_create_pair (&handle->nl80211_scan_results, NETLINK_GENERIC);
	nl_socket_modify_cb (handle->nl80211_scan_results.nl_sock, NL_CB_VALID, NL_CB_CUSTOM, wireless_bss_parse_station_scan, handle);
	nl_socket_modify_cb (handle->nl80211_scan_results.nl_sock, NL_CB_FINISH, NL_CB_CUSTOM, wireless_bss_finish_scan, handle);
	
	/* Guardar la familia nl80211 */
	wireless_if_nl80211_id = family_id;
	handle->nl_sock_nl80211 = sock_req;
	
	/* Instalar un timer para ejecutar TRIGGER_SCAN cada minuto */
	//g_timeout_add_seconds (50, wireless_interface_timer_trigger, handle);
}

int wireless_events_dispatcher (struct nl_msg *msg, void *arg) {
	NetworkInadorHandle *handle = (NetworkInadorHandle *) arg;
	struct nlmsghdr *reply;
	struct genlmsghdr *gnlh;
	struct nlattr *attr;
	int remaining;
	Interface *iface;
	int has_bss;
	int index;
	
	reply = nlmsg_hdr (msg);
	
	if (reply->nlmsg_type != wireless_if_nl80211_id) return NL_SKIP;
	
	gnlh = nlmsg_data (reply);
	
	switch (gnlh->cmd) {
		case NL80211_CMD_NEW_SCAN_RESULTS:
			/* Es un mensaje del grupo multicast "scan" */
			has_bss = 0;
			index = 0;
			iface = NULL;
			nlmsg_for_each_attr(attr, reply, sizeof (struct genlmsghdr), remaining) {
				if (nla_type (attr) == NL80211_ATTR_BSS) {
					has_bss = 1;
				} else if (nla_type (attr) == NL80211_ATTR_IFINDEX) {
					if (nla_len (attr) != 4) {
						/* Tamaño incorrecto para el nuevo master */
						continue;
					}
					index = nla_get_u32 (attr);
					printf ("CMD_NEW_SCAN_RESULTS sobre interfaz %i\n", index);
				}
			}
			
			if (index > 0) {
				iface = _interfaces_locate_by_index (handle->interfaces, index);
			}
			
			if (iface == NULL) {
				/* Si no hay interfaz, tenemos un gran problema */
				break;
			}
			
			if (has_bss == 1) {
				/* Es un mensaje de una estación */
				printf ("Mensaje NEW RESULTS con estación\n");
			} else {
				/* Ejecutar el CMD_GET_SCAN */
				printf ("Ejecutando GET_SCAN...\n");
				wireless_interface_get_scan (handle, iface);
			}
			return NL_SKIP;
			break;
	}
	
	return NL_SKIP;
}

void wireless_interface_trigger_scan (NetworkInadorHandle *handle, Interface *iface) {
	struct nl_msg *msg;
	int error, ret;
	struct nlattr *ssid;
	
	msg = nlmsg_alloc ();
	if (msg == NULL) {
		return;
	}
	
	if (iface->is_wireless == 0) {
		/* No puedo ejecutar trigger scan sobre una interfaz que no he visto como wireless */
		return;
	}
	
	printf ("Ejecutando NL80211_CMD_TRIGGER_SCAN\n");
	
	genlmsg_put (msg, NL_AUTO_PORT, NL_AUTO_SEQ, wireless_if_nl80211_id, 0, 0, NL80211_CMD_TRIGGER_SCAN, 0);
	
	nla_put_u32 (msg, NL80211_ATTR_IFINDEX, iface->index);
	
	ssid = nla_nest_start (msg, NL80211_ATTR_SCAN_SSIDS);
	nla_put (msg, 1, 0, "");  // Scan all SSIDs.
	nla_nest_end (msg, ssid);
	
	nl_complete_msg (handle->nl_sock_nl80211, msg);
	
	ret = nl_send (handle->nl_sock_nl80211, msg);
	
	nlmsg_free (msg);
	if (ret <= 0) {
		return;
	}
	
	error = 0;
	nl_socket_modify_cb (handle->nl_sock_nl80211, NL_CB_VALID, NL_CB_CUSTOM, _wireless_if_valid, &error);
	nl_socket_modify_cb (handle->nl_sock_nl80211, NL_CB_INVALID, NL_CB_CUSTOM, _wireless_if_invalid, &error);
	nl_socket_modify_cb (handle->nl_sock_nl80211, NL_CB_ACK, NL_CB_CUSTOM, _wireless_if_ack, &error);
	nl_socket_modify_err_cb (handle->nl_sock_nl80211, NL_CB_CUSTOM, _wireless_if_wait_error, &error);
	
	nl_recvmsgs_default (handle->nl_sock_nl80211);
	
	if (ret <= 0 || error < 0) {
		printf ("Error al hacer NL80211_CMD_TRIGGER_SCAN error = %i\n", error);
		return;
	}
}

void wireless_interface_get_scan (NetworkInadorHandle *handle, Interface *iface) {
	struct nl_msg *msg;
	int error, ret;
	struct genlmsghdr *gnlh;
	struct nlmsghdr *req;
	
	msg = nlmsg_alloc ();
	if (msg == NULL) {
		return;
	}
	
	if (iface->is_wireless == 0) {
		/* No puedo ejecutar Get scan sobre una interfaz que no he visto como wireless */
		return;
	}
	
	printf ("Ejecutando CMD_GET_SCAN\n");
	
	req = nlmsg_put (msg, NL_AUTO_PORT, NL_AUTO_SEQ, wireless_if_nl80211_id, sizeof (struct genlmsghdr), NLM_F_DUMP);
	gnlh = (struct genlmsghdr *) nlmsg_data (req);
	
	gnlh->cmd = NL80211_CMD_GET_SCAN;
	gnlh->version = 0;
	
	nla_put_u32 (msg, NL80211_ATTR_IFINDEX, iface->index);
	
	nl_complete_msg (handle->nl80211_scan_results.nl_sock, msg);
	
	ret = nl_send (handle->nl80211_scan_results.nl_sock, msg);
	
	nlmsg_free (msg);
	if (ret <= 0) {
		return;
	}
	
	//nl_socket_modify_cb (handle->nl80211_scan_results, NL_CB_VALID, NL_CB_CUSTOM, _wireless_if_valid, &error);
	/*nl_socket_modify_cb (handle->nl80211_scan_results, NL_CB_INVALID, NL_CB_CUSTOM, _wireless_if_invalid, &error);
	nl_socket_modify_cb (handle->nl80211_scan_results, NL_CB_ACK, NL_CB_CUSTOM, _wireless_if_ack, &error);
	nl_socket_modify_err_cb (handle->nl80211_scan_results, NL_CB_CUSTOM, _wireless_if_wait_error, &error);*/
	
	//nl_recvmsgs_default (handle->nl_sock_nl80211);
	
	if (ret <= 0) {
		printf ("Error al hacer CMD_GET_SCAN error = %i\n", error);
		//return;
	}
	
	//nl_recvmsgs_default (handle->nl_sock_nl80211);
}

void wireless_interface_check (NetworkInadorHandle *handle, Interface *iface) {
	struct nl_msg * msg;
	int ret;
	struct _wireless_iface_is_wifi is_wifi;
	WirelessInfo *info;
	
	if (wireless_if_nl80211_id == 0) {
		wireless_interface_init_nl80211 (handle);
	}
	
	info = (WirelessInfo *) malloc (sizeof (WirelessInfo));
	
	if (info == NULL) {
		return;
	}
	
	msg = nlmsg_alloc ();
	if (msg == NULL) {
		free (info);
		return;
	}
	
	genlmsg_put (msg, NL_AUTO_PORT, NL_AUTO_SEQ, wireless_if_nl80211_id, 0, 0, NL80211_CMD_GET_WIPHY, 0);  // Setup which command to run.
	nla_put_u32 (msg, NL80211_ATTR_IFINDEX, iface->index);
	
	nl_complete_msg (handle->nl_sock_nl80211, msg);
	
	ret = nl_send (handle->nl_sock_nl80211, msg);
	
	nlmsg_free (msg);
	if (ret <= 0) {
		free (info);
		return;
	}
	
	memset (&is_wifi, 0, sizeof (is_wifi));
	memset (info, 0, sizeof (WirelessInfo));
	is_wifi.info = info;
	
	nl_socket_modify_cb (handle->nl_sock_nl80211, NL_CB_VALID, NL_CB_CUSTOM, _wireless_if_cb_valid_is_wifi, &is_wifi);
	nl_socket_modify_cb (handle->nl_sock_nl80211, NL_CB_INVALID, NL_CB_CUSTOM, _wireless_if_invalid, &is_wifi.error);
	nl_socket_modify_cb (handle->nl_sock_nl80211, NL_CB_ACK, NL_CB_CUSTOM, _wireless_if_ack, &is_wifi.error);
	nl_socket_modify_err_cb (handle->nl_sock_nl80211, NL_CB_CUSTOM, _wireless_if_wait_error, &is_wifi.error);
	
	nl_recvmsgs_default (handle->nl_sock_nl80211);
	
	if (ret <= 0 || is_wifi.error < 0) {
		free (info);
		return;
	}
	
	if (is_wifi.is_wifi != 0) {
		/* Es una interfaz inalámbrica */
		printf ("La interfaz %s es inalambrica\n", iface->name);
		
		iface->is_wireless = 1;
		iface->wireless = info;
	} else {
		free (info);
	}
}
