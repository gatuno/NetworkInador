/*
 * main.c
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

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

#include <assert.h>

#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#include <glib.h>

#include <netlink/socket.h>
#include <netlink/msg.h>

#include "common.h"
#include "interfaces.h"
#include "netlink-events.h"

/* Usados para salir en caso de una señal */
static int sigterm_pipe_fds[2] = { -1, -1 };

static void _init_handle (NetworkInadorHandle *handle) {
	assert (handle != NULL);
	
	memset (handle, 0, sizeof (NetworkInadorHandle));
	handle->interfaces = NULL;
}

static void _sigterm_handler (int signum) {
	//fprintf (stderr, "SIGTERM SIGINT Handler\n");
	if (sigterm_pipe_fds[1] >= 0) {
		if (write (sigterm_pipe_fds[1], "", 1) == -1 ) {
			//fprintf (stderr, "Write to sigterm_pipe failed.\n");
		}
		close (sigterm_pipe_fds[1]);
		sigterm_pipe_fds[1] = -1;
	}
}

static gboolean _main_quit_handler (GIOChannel *source, GIOCondition cond, gpointer data) {
	GMainLoop *loop = (GMainLoop *) data;
	g_main_loop_quit (loop);
}

static void _main_setup_signal (void *loop) {
	struct sigaction act;
	sigset_t empty_mask;
	
	/* Preparar el pipe para la señal de cierre */
	if (pipe (sigterm_pipe_fds) != 0) {
		perror ("Failed to create SIGTERM pipe");
		sigterm_pipe_fds[0] = -1;
	}
	
	/* Instalar un manejador de señales para SIGTERM */
	sigemptyset (&empty_mask);
	act.sa_mask    = empty_mask;
	act.sa_flags   = 0;
	act.sa_handler = &_sigterm_handler;
	if (sigaction (SIGTERM, &act, NULL) < 0) {
		perror ("Failed to register SIGTERM handler");
	}
	
	if (sigaction (SIGINT, &act, NULL) < 0) {
		perror ("Failed to register SIGINT handler");
	}
	
	if (sigterm_pipe_fds[0] != -1) {
		GIOChannel *io;
		
		io = g_io_channel_unix_new (sigterm_pipe_fds[0]);
		g_io_channel_set_close_on_unref (io, TRUE);
		
		g_io_add_watch (io, G_IO_IN | G_IO_PRI | G_IO_HUP | G_IO_ERR, _main_quit_handler, loop);
	}
}

int main (int argc, char *argv[]) {
	NetworkInadorHandle handle;
	GMainLoop *loop = NULL;
	struct nl_sock * sock_req;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif
	
	_init_handle (&handle);
	
	/* Crear el socket de peticiones */
	sock_req = nl_socket_alloc ();
	
	if (nl_connect (sock_req, NETLINK_ROUTE) != 0) {
		perror ("Falló conectar netlink socket\n");
		
		return -1;
	}
	
	handle.nl_sock_route = sock_req;
	
	/* Crear el socket que escucha eventos */
	netlink_events_setup (&handle);
	
	loop = g_main_loop_new (NULL, FALSE);
	
	_main_setup_signal (loop);
	
	interfaces_init (&handle);
	
	g_main_loop_run (loop);
	
	/* Detener la llegada de eventos */
	netlink_events_clear (&handle);
	
	// nl_socket_free???
	
	return 0;
}
