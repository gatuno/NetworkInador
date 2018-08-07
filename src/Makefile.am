# Automake file for NetworkInador

bin_PROGRAMS = network-inador
network_inador_SOURCES = network-inador.c network-inador.h \
	events.c events.h \
	interfaces.c interfaces.h \
	utils.c utils.h

#network_inador_CPPFLAGS = -DGAMEDATA_DIR=\"$(gamedatadir)/\" -DLOCALEDIR=\"$(localedir)\" $(AM_CPPFLAGS)
network_inador_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\" $(AM_CPPFLAGS)
network_inador_CFLAGS = $(GLIB_CFLAGS) $(AM_CFLAGS)
network_inador_LDADD = $(GLIB_LIBS)
LDADD = $(LIBINTL)

