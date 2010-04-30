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

AC_CHECK_HEADERS([security/pam_appl.h],[],[AC_MSG_ERROR([PAM not found])])

AC_PATH_PROG(DBUSBINDINGTOOL, dbus-binding-tool)
AC_SUBST(DBUSBINDINGTOOL)

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
po/Makefile.in
src/Makefile
])
