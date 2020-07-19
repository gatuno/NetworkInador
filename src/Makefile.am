# Automake file for NetworkInador

bin_PROGRAMS = network-inador
network_inador_SOURCES = main.c common.h \
	netlink-events.c netlink-events.h \
	interfaces.c interfaces.h \
	ip-address.c ip-address.h \
	wireless_if.c wireless_if.h \
	wireless_bss.c wireless_bss.h

#network_inador_CPPFLAGS = -DGAMEDATA_DIR=\"$(gamedatadir)/\" -DLOCALEDIR=\"$(localedir)\" $(AM_CPPFLAGS)
network_inador_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\" $(AM_CPPFLAGS)
network_inador_CFLAGS = $(GLIB_CFLAGS) $(LIBNL3_CFLAGS) $(LIBNLGEN3_CFLAGS) $(AM_CFLAGS)
network_inador_LDADD = $(GLIB_LIBS) $(LIBNL3_LIBS) $(LIBNLGEN3_LIBS)
LDADD = $(LIBINTL)

#SUBDIRS = client_test dhcpc
