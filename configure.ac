dnl Process this file with autoconf to produce a configure script.

AC_INIT(lightdm, 1.0.0)
AC_CONFIG_MACRO_DIR(m4)
AC_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE
LT_INIT
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_LIBTOOL
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])

GOBJECT_INTROSPECTION_CHECK(0.9.5)

GNOME_COMPILE_WARNINGS(maximum)

dnl ###########################################################################
dnl Dependencies
dnl ###########################################################################

AC_CHECK_HEADERS(security/pam_appl.h, [], AC_MSG_ERROR(PAM not found))

PKG_CHECK_MODULES(LIGHTDM, [
    glib-2.0
    gio-2.0 >= 2.26
    gio-unix-2.0
    xdmcp
    xcb
])

PKG_CHECK_MODULES(GLIB, [
    glib-2.0
])

PKG_CHECK_MODULES(GIO, [
    gio-2.0
])

PKG_CHECK_MODULES(GIO_UNIX, [
    gio-unix-2.0
])

PKG_CHECK_MODULES(GOBJECT, [
    gobject-2.0
])

PKG_CHECK_MODULES(XCB, [
    xcb
])    

AC_ARG_ENABLE(liblightdm-gobject,
	AS_HELP_STRING([--enable-liblightdm-gobject],[Enable LightDM client gobject libraries [[default=yes]]]),
	[enable_liblightdm_gobject=$enableval],
	[enable_liblightdm_gobject="yes"])
compile_liblightdm_gobject=no
if test x"$enable_liblightdm_gobject" = "xauto"; then
    PKG_CHECK_MODULES(LIBLIGHTDM_GOBJECT, [
        glib-2.0
        gio-2.0 >= 2.26
        gio-unix-2.0
        gobject-2.0
        libxklavier
        x11
    ], compile_liblightdm_gobject=yes, compile_liblightdm_gobject=no)
elif test x"$enable_liblightdm_gobject" = "xyes"; then
    PKG_CHECK_MODULES(LIBLIGHTDM_GOBJECT, [
        glib-2.0
        gio-2.0 >= 2.26
        gio-unix-2.0
        gobject-2.0
        libxklavier
        x11
    ])
    compile_liblightdm_gobject=yes
fi
AM_CONDITIONAL(COMPILE_LIBLIGHTDM_GOBJECT, test x"$compile_liblightdm_gobject" != "xno")

AC_ARG_ENABLE(liblightdm-qt,
	AS_HELP_STRING([--enable-liblightdm-qt],[Enable LightDM client QT libraries [[default=auto]]]),
	[enable_liblightdm_qt=$enableval],
	[enable_liblightdm_qt="auto"])
compile_liblightdm_qt=no
if test x"$enable_liblightdm_qt" = "xauto"; then
    PKG_CHECK_MODULES(LIBLIGHTDM_QT, [
        QtCore
        QtDBus
        QtNetwork
    ], compile_liblightdm_qt=yes, compile_liblightdm_qt=no)
elif test x"$enable_liblightdm_qt" = "xyes"; then
    PKG_CHECK_MODULES(LIBLIGHTDM_QT, [
        QtCore
        QtDBus
        QtNetwork
    ])
    compile_liblightdm_qt=yes
fi
AM_CONDITIONAL(COMPILE_LIBLIGHTDM_QT, test x"$compile_liblightdm_qt" != "xno")

have_qt=no
if test x"$compile_liblightdm_qt" = "xyes"; then
    PKG_CHECK_MODULES(LIGHTDM_QT_GREETER, [
        QtCore
        QtGui
    ], have_qt=yes, have_qt=no)
fi
AM_CONDITIONAL(HAVE_QT_GREETER, test $have_qt = yes)

PKG_CHECK_MODULES(LIGHTDM_GTK_GREETER, [
    gtk+-3.0
    gmodule-export-2.0
], have_gtk=yes, have_gtk=no)
AM_CONDITIONAL(HAVE_GTK_GREETER, test $have_gtk = yes)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(DBUSBINDINGTOOL, dbus-binding-tool)
AC_SUBST(DBUSBINDINGTOOL)

dnl ###########################################################################
dnl Configurable values
dnl ###########################################################################

USER_SESSION=default
AC_ARG_WITH(user-session,
            AS_HELP_STRING(--with-user-session=<name>,
                           Session to use for user accounts),
    if test x$withval != x; then
        USER_SESSION="$withval"
    fi
)
AC_SUBST(USER_SESSION)
AC_DEFINE_UNQUOTED(USER_SESSION, "$USER_SESSION", User session)

if test $have_gtk = yes; then
GREETER_SESSION=lightdm-gtk-greeter
elif test $have_qt = yes; then
GREETER_SESSION=lightdm-qt-greeter
else
GREETER_SESSION=
fi
AC_ARG_WITH(greeter-session,
            AS_HELP_STRING(--with-greeter-session=<session>,
                           Greeter session),
    if test x$withval != x; then
        GREETER_SESSION="$withval"
    fi
)
AC_SUBST(GREETER_SESSION)
AC_DEFINE_UNQUOTED(GREETER_SESSION, "$GREETER_SESSION", Greeter session)

GREETER_USER=lightdm
AC_ARG_WITH(greeter-user,
            AS_HELP_STRING(--with-greeter-user=<username>,
                           User to run greeter as),
    if test x$withval != x; then
        GREETER_USER="$withval"
    fi
)
AC_SUBST(GREETER_USER)
AC_DEFINE_UNQUOTED(GREETER_USER, "$GREETER_USER", User to run greeter as)

dnl ###########################################################################
dnl Documentation
dnl ###########################################################################

GTK_DOC_CHECK

dnl ###########################################################################
dnl Internationalization
dnl ###########################################################################

IT_PROG_INTLTOOL(0.35.0)
AC_SUBST(GETTEXT_PACKAGE, lightdm)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", Gettext package)

dnl ###########################################################################
dnl Files to generate
dnl ###########################################################################

AC_CONFIG_FILES([
Makefile
data/Makefile
data/init/Makefile
doc/Makefile
greeters/Makefile
greeters/gtk/Makefile
greeters/qt/Makefile
liblightdm-gobject/liblightdm-gobject-1.pc
liblightdm-gobject/Makefile
liblightdm-qt/Makefile
liblightdm-qt/liblightdm-qt-1.pc
po/Makefile.in
src/Makefile
tests/Makefile
tests/src/Makefile
utils/Makefile
])
AC_OUTPUT

dnl ###########################################################################
dnl Summary
dnl ###########################################################################

echo "
                    Light Display Manager $VERSION
                    ===========================

        prefix:                   $prefix
        Greeter session:          $GREETER_SESSION
        Greeter user:             $GREETER_USER
        User session:             $USER_SESSION
        liblightdm-gobject:       $compile_liblightdm_gobject
        GObject introspection:    $found_introspection
        liblightdm-qt:            $compile_liblightdm_qt
        GTK+ Greeter:             $have_gtk
        Qt Greeter:               $have_qt
"
