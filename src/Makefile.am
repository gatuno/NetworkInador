# Automake file for NetworkInador

bin_PROGRAMS = network-inador
network_inador_SOURCES = network-inador.c network-inador.h \
	netlink-events.c netlink-events.h \
	interfaces.c interfaces.h \
	manager.c manager.h \
	utils.c utils.h \
	bridge.c bridge.h \
	rta_aux.c rta_aux.h \
	routes.c routes.h \
	manager-events.c manager-events.h \
	dhcp.c dhcp.h

#network_inador_CPPFLAGS = -DGAMEDATA_DIR=\"$(gamedatadir)/\" -DLOCALEDIR=\"$(localedir)\" $(AM_CPPFLAGS)
network_inador_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\" $(AM_CPPFLAGS)
network_inador_CFLAGS = $(GLIB_CFLAGS) $(AM_CFLAGS)
network_inador_LDADD = $(GLIB_LIBS)
LDADD = $(LIBINTL)

SUBDIRS = dhcpc
