AC_INIT([NetworkInador], [0.1.0], [], [networkinador])

AC_REVISION([Revision 1])

AC_CONFIG_AUX_DIR([build-aux])

AC_CONFIG_SRCDIR([src/main.c])

# For debian systems, /usr as default
AC_PREFIX_DEFAULT([/usr])

# We need Gcc
AC_PROG_CC

# and automake
AM_INIT_AUTOMAKE([-Wall -Werror])

# Translate this program
AM_GNU_GETTEXT_VERSION([0.19.8])
AM_GNU_GETTEXT([external])

ALL_LINGUAS=""
AC_SUBST(ALL_LINGUAS)

AM_PROG_CC_C_O

# Revisar el host
AC_CANONICAL_HOST

case $host_os in
  *linux* ) LINUX=yes;;
        * )
        AC_MSG_ERROR([Only linux systems are supported])
        ;;
esac

GLIB_VERSION=2.50
LIBNL_VERSION=0.29

AC_MSG_CHECKING([if you have Glib installed on your system])
PKG_CHECK_EXISTS([glib-2.0 >= $GLIB_VERSION], [AC_MSG_RESULT([yes])], [AC_MSG_FAILURE([Glib not found in your system])])
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= $GLIB_VERSION], [], [])

AC_MSG_CHECKING([if you have libnl-3 installed on your system])
PKG_CHECK_EXISTS([libnl-3.0 >= LIBNL_VERSION], [AC_MSG_RESULT([yes])], [AC_MSG_FAILURE([libnl-3 not found in your system])])
PKG_CHECK_MODULES(LIBNL3, [libnl-3.0 >= $LIBNL_VERSION], [], [])

AC_MSG_CHECKING([if you have libnl-gen-3 installed on your system])
PKG_CHECK_EXISTS([libnl-genl-3.0 >= LIBNL_VERSION], [AC_MSG_RESULT([yes])], [AC_MSG_FAILURE([libnl-gen-3 not found in your system])])
PKG_CHECK_MODULES(LIBNLGEN3, [libnl-genl-3.0 >= $LIBNL_VERSION], [], [])

AC_CONFIG_HEADERS([config.h])

AC_CONFIG_FILES([
                 Makefile
                 src/Makefile
                 po/Makefile.in
])
#                 src/dhcpc/Makefile
#                 src/client_test/Makefile
#                 data/Makefile

AC_OUTPUT
