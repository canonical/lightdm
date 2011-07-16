dnl Process this file with autoconf to produce a configure script.

AC_INIT(lightdm, 0.4.5)
AC_CONFIG_MACRO_DIR(m4)
AC_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE
LT_INIT
AM_PROG_CC_C_O
AM_PROG_VALAC
AC_PROG_CXX
AC_PROG_LIBTOOL
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])

GOBJECT_INTROSPECTION_CHECK(0.9.5)

AC_PATH_PROG([VALA_API_GEN], [vapigen])

GNOME_COMPILE_WARNINGS(maximum)

dnl ###########################################################################
dnl Dependencies
dnl ###########################################################################

AC_CHECK_HEADERS(security/pam_appl.h, [], AC_MSG_ERROR(PAM not found))

PKG_CHECK_MODULES(LIGHTDM, [
    glib-2.0
    gio-2.0 >= 2.26
    gio-unix-2.0
    xcb
    xdmcp
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

if test x"$VALAC" = "x"; then
    have_vala=no
else
    have_vala=yes
fi
AM_CONDITIONAL(HAVE_VALA_GTK_GREETER, test $have_vala = yes)

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
    PKG_CHECK_MODULES(LIGHTDM_EXAMPLE_QT_GREETER, [
        QtCore
        QtGui
    ], have_qt=yes, have_qt=no)
fi
AM_CONDITIONAL(HAVE_QT_GREETER, test $have_qt = yes)

PKG_CHECK_MODULES(LIGHTDM_EXAMPLE_GTK_GREETER, [
    gtk+-2.0
    gmodule-export-2.0
], have_gtk=yes, have_gtk=no)
AM_CONDITIONAL(HAVE_GTK_GREETER, test $have_gtk = yes)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(DBUSBINDINGTOOL, dbus-binding-tool)
AC_SUBST(DBUSBINDINGTOOL)

GREETER_THEME_DIR=${datarootdir}/lightdm/themes
AC_SUBST(GREETER_THEME_DIR)

GREETER_THEME_ENGINE_DIR=${libexecdir}
AC_SUBST(GREETER_THEME_ENGINE_DIR)

dnl ###########################################################################
dnl Configurable values
dnl ###########################################################################

DEFAULT_XSESSION=gnome
AC_ARG_WITH(default-xsession,
            AS_HELP_STRING(--with-default-xsession=<name>,
                           Default X session to launch),
    if test x$withval != x; then
        DEFAULT_XSESSION="$withval"
    fi
)
AC_SUBST(DEFAULT_XSESSION)
AC_DEFINE_UNQUOTED(DEFAULT_XSESSION, "$DEFAULT_XSESSION", Default X session)

DEFAULT_GREETER_THEME=example-gtk-gnome
AC_ARG_WITH(theme,
            AS_HELP_STRING(--with-theme=<theme>,
                           Default theme),
    if test x$withval != x; then
        DEFAULT_GREETER_THEME="$withval"
    fi
)
AC_SUBST(DEFAULT_GREETER_THEME)
AC_DEFINE_UNQUOTED(DEFAULT_GREETER_THEME, "$DEFAULT_GREETER_THEME", Default greeter theme)

dnl ###########################################################################
dnl Documentation
dnl ###########################################################################

GTK_DOC_CHECK

dnl ###########################################################################
dnl Internationalization
dnl ###########################################################################

IT_PROG_INTLTOOL(0.35.0)
GETTEXT_PACKAGE=lightdm
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
greeters/python-gtk/Makefile
greeters/qt/Makefile
greeters/vala-gtk/Makefile
liblightdm-gobject/liblightdm-gobject-0.pc
liblightdm-gobject/Makefile
liblightdm-gobject/lightdm/Makefile
liblightdm-qt/Makefile
liblightdm-qt/QLightDM/liblightdm-qt-0.pc
liblightdm-qt/QLightDM/Makefile
po/Makefile.in
src/Makefile
tests/Makefile
tests/src/Makefile
themes/Makefile
themes/example-gtk-gnome/Makefile
themes/example-python-gtk-gnome/Makefile
themes/example-qt-kde/Makefile
themes/example-vala-gtk-gnome/Makefile
],
[chmod +x tests/src/test-setup-guest tests/src/test-cleanup-guest])
AC_OUTPUT

dnl ###########################################################################
dnl Summary
dnl ###########################################################################

echo "
                    Light Display Manager $VERSION
                    ===========================

        prefix:                   $prefix
        Default X session:        $DEFAULT_XSESSION
        Default greeter theme:    $DEFAULT_GREETER_THEME

        Greeters:
        liblightdm-gobject:       $compile_liblightdm_gobject
        GObject introspection:    $found_introspection
        liblightdm-qt:            $compile_liblightdm_qt
        GTK+ Greeter:             $have_gtk
        Vala Greeter:             $have_vala
        Qt Greeter:               $have_qt
"
