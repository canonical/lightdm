dnl Process this file with autoconf to produce a configure script.

AC_INIT(configure.ac)
AM_INIT_AUTOMAKE(lightdm, 0.0.1)
AM_MAINTAINER_MODE
GNOME_MAINTAINER_MODE_DEFINES
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_ISC_POSIX
AC_PROG_CC_C_O
AM_PROG_CC_STDC
AC_HEADER_STDC

AM_GCONF_SOURCE_2

GNOME_COMPILE_WARNINGS(yes)

dnl ###########################################################################
dnl Dependencies
dnl ###########################################################################

PKG_CHECK_MODULES(LIGHTDM, [
    glib-2.0
    gobject-2.0
    dbus-glib-1
    ck-connector
])

PKG_CHECK_MODULES(LIGHTDM_GREETER, [
    gtk+-2.0
    dbus-glib-1
])

PKG_CHECK_MODULES(LIGHTDM_WKGREETER, [
    gtk+-2.0
    webkit-1.0
    dbus-glib-1
])

AC_CHECK_HEADERS([security/pam_appl.h],[],[AC_MSG_ERROR([PAM not found])])

AC_PATH_PROG(DBUSBINDINGTOOL, dbus-binding-tool)
AC_SUBST(DBUSBINDINGTOOL)

dnl ###########################################################################
dnl Check for D-Bus
dnl ###########################################################################

dnl - Are we specifying a different dbus root ?
AC_ARG_WITH(dbus-sys,
            AS_HELP_STRING([--with-dbus-sys=<dir>],
                           [where D-BUS system.d directory is]))
AC_ARG_WITH(dbus-services,
            AS_HELP_STRING([--with-dbus-services=<dir>],
                           [where D-BUS services directory is]))
            
if ! test -z "$with_dbus_sys" ; then
    DBUS_SYS_DIR="$with_dbus_sys"
else
    DBUS_SYS_DIR="$sysconfdir/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

dnl ###########################################################################
dnl Configurable values
dnl ###########################################################################

AC_ARG_WITH(config-file,
            AS_HELP_STRING([--config-file=<file>],
                           [Configuration file to load]))
if test x$withval != x; then
    CONFIG_FILE="$withval"
else
    CONFIG_FILE=${sysconfdir}/lightdm.conf
fi
AC_SUBST(CONFIG_FILE)

AC_ARG_WITH(xserver-binary,
            AS_HELP_STRING([--xserver-binary=<binary>],
                           [X server binary name]))
if test x$withval != x; then
    XSERVER_BINARY="$withval"
else
    XSERVER_BINARY=${prefix}/bin/X
fi
AC_SUBST(XSERVER_BINARY)

AC_ARG_WITH(xsession-dir,
            AS_HELP_STRING([--xsession-dir=<dir>],
                           [X session directory]))
if test x$withval != x; then
    XSESSION_DIR="$withval"
else
    XSESSION_DIR=${prefix}/share/xsessions
fi
AC_SUBST(XSESSION_DIR)

AC_ARG_WITH(default-session,
            AS_HELP_STRING([--default-session=<name>],
                           [Default session to launch]))
if test x$withval != x; then
    DEFAULT_SESSION="$withval"
else
    DEFAULT_SESSION=gnome
fi
AC_SUBST(DEFAULT_SESSION)

AC_ARG_WITH(greeter-user,
            AS_HELP_STRING([--greeter-user=<username>],
                           [User to run greeter as]))
if test x$withval != x; then
    GREETER_USER="$withval"
else
    GREETER_USER=gdm
fi
AC_SUBST(GREETER_USER)

AC_ARG_WITH(greeter-binary,
            AS_HELP_STRING([--greeter-binary=<binary>],
                           [Greeter to execute]))
if test x$withval != x; then
    GREETER_BINARY="$withval"
else
    GREETER_BINARY=${prefix}/bin/ldmgreeter
fi
AC_SUBST(GREETER_BINARY)

dnl ###########################################################################
dnl Documentation
dnl ###########################################################################

GNOME_DOC_INIT

dnl ###########################################################################
dnl Internationalization
dnl ###########################################################################

IT_PROG_INTLTOOL([0.35.0])
GETTEXT_PACKAGE=lightdm
AC_SUBST(GETTEXT_PACKAGE)
AM_GLIB_GNU_GETTEXT

dnl ###########################################################################
dnl Files to generate
dnl ###########################################################################

AC_OUTPUT([
Makefile
data/Makefile
po/Makefile.in
src/Makefile
])

dnl ###########################################################################
dnl Summary
dnl ###########################################################################

echo "
                    Light Display Manager $VERSION
                    ===========================

        prefix:                   ${prefix}
        Config file:              ${CONFIG_FILE}
        XServer binary:           ${XSERVER_BINARY}
        XSessions dir:            ${XSESSION_DIR}
        Default session:          ${DEFAULT_SESSION}
        Greeter user:             ${GREETER_USER}
        Greeter binary:           ${GREETER_BINARY}
"
