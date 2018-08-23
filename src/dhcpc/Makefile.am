# Automake file for NetworkInador DHCP Client

sbin_PROGRAMS = nidhcpc
nidhcpc_SOURCES = nidhcpc.c \
	common.c common.h \
	dhcpc.c dhcpc.h dhcpd.h \
	extra.c extra.h \
	packet.c \
	signalpipe.c \
	socket.c

#client_network_CPPFLAGS = -DGAMEDATA_DIR=\"$(gamedatadir)/\" -DLOCALEDIR=\"$(localedir)\" $(AM_CPPFLAGS)
nidhcpc_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\" $(AM_CPPFLAGS)
nidhcpc_CFLAGS = $(AM_CFLAGS)
nidhcpc_LDADD = 
LDADD = $(LIBINTL)

