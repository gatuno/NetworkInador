AC_INIT([NetworkInador], [0.1.0], [], [networkinador])

AC_REVISION([Revision 1])

AC_CONFIG_AUX_DIR([build-aux])

AC_CONFIG_SRCDIR([src/network-inador.c])

# For debian systems, /usr as default
AC_PREFIX_DEFAULT([/usr])

# We need Gcc
AC_PROG_CC
# We need OBJC, for MAC
AC_PROG_OBJC
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

AC_MSG_CHECKING([if you have Glib installed on your system])
PKG_CHECK_EXISTS([glib-2.0 >= $GLIB_VERSION], [AC_MSG_RESULT([yes])], [AC_MSG_FAILURE([Glib not found in your system])])
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= $GLIB_VERSION], [], [])


AC_CONFIG_HEADERS([config.h])

AC_CONFIG_FILES([
                 Makefile
                 src/Makefile
                 po/Makefile.in
                 src/dhcpc/Makefile
])
#                 data/Makefile

AC_OUTPUT
