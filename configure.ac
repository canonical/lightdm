dnl Process this file with autoconf to produce a configure script.

AC_INIT(lightdm, 0.0.1)
AM_INIT_AUTOMAKE
AM_SILENT_RULES([yes])
LT_INIT
AM_PROG_CC_C_O
AM_MAINTAINER_MODE

GOBJECT_INTROSPECTION_CHECK([0.6.7])

AM_GCONF_SOURCE_2

GNOME_COMPILE_WARNINGS(yes)

dnl ###########################################################################
dnl Dependencies
dnl ###########################################################################

AC_CHECK_HEADERS([security/pam_appl.h],[],[AC_MSG_ERROR([PAM not found])])

PKG_CHECK_MODULES(LIGHTDM, [
    glib-2.0
    gobject-2.0
    dbus-glib-1
    ck-connector
])

PKG_CHECK_MODULES(LIBLDMGREETER, [
    glib-2.0
    gobject-2.0
    dbus-glib-1
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
dnl Configurable values
dnl ###########################################################################

CONFIG_FILE=${sysconfdir}/lightdm.conf
AC_ARG_WITH(config-file,
            AS_HELP_STRING([--with-config-file=<file>],
                           [Configuration file to load]),
    if test x$withval != x; then
        CONFIG_FILE="$withval"
    fi
)    
AC_SUBST(CONFIG_FILE)

XSERVER_BINARY=/usr/bin/X
AC_ARG_WITH(xserver-binary,
            AS_HELP_STRING([--with-xserver-binary=<binary>],
                           [X server binary name]),
    if test x$withval != x; then
        XSERVER_BINARY="$withval"
    fi
)    
AC_SUBST(XSERVER_BINARY)

XSESSION_DIR=/usr/share/xsessions
AC_ARG_WITH(xsession-dir,
            AS_HELP_STRING([--with-xsession-dir=<dir>],
                           [X session directory]),
    if test x$withval != x; then
        XSESSION_DIR="$withval"
    fi
)    
AC_SUBST(XSESSION_DIR)

DEFAULT_SESSION=gnome
AC_ARG_WITH(default-session,
            AS_HELP_STRING([--with-default-session=<name>],
                           [Default session to launch]),
    if test x$withval != x; then
        DEFAULT_SESSION="$withval"
    fi
)
AC_SUBST(DEFAULT_SESSION)

GREETER_USER=gdm
AC_ARG_WITH(greeter-user,
            AS_HELP_STRING([--with-greeter-user=<username>],
                           [User to run greeter as]),
    if test x$withval != x; then
        GREETER_USER="$withval"
    fi
)
AC_SUBST(GREETER_USER)

THEME_DIR=${prefix}/share/lightdm/themes
AC_ARG_WITH(theme-dir,
            AS_HELP_STRING([--with-theme-dir=<dir>],
                           [Directory containing greeter themes]),
    if test x$withval != x; then
        THEME_DIR="$withval"
    fi
)
AC_SUBST(THEME_DIR)

GREETER_THEME=gnome
AC_ARG_WITH(theme,
            AS_HELP_STRING([--with-theme=<theme>],
                           [Default theme]),
    if test x$withval != x; then
        GREETER_THEME="$withval"
    fi
)
AC_SUBST(GREETER_THEME)

dnl ###########################################################################
dnl Documentation
dnl ###########################################################################

GNOME_DOC_INIT
GTK_DOC_CHECK

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
doc/Makefile
greeters/Makefile
libldmgreeter/libldmgreeter-1.pc
libldmgreeter/Makefile
po/Makefile.in
src/Makefile
themes/Makefile
themes/webkit/Makefile
])

dnl ###########################################################################
dnl Summary
dnl ###########################################################################

echo "
                    Light Display Manager $VERSION
                    ===========================

        prefix:                   ${prefix}
        Config file:              ${CONFIG_FILE}
        X server binary:          ${XSERVER_BINARY}
        XSessions dir:            ${XSESSION_DIR}
        Default session:          ${DEFAULT_SESSION}
        Greeter user:             ${GREETER_USER}
        Theme directory:          ${THEME_DIR}
        Theme:                    ${GREETER_THEME}
"
