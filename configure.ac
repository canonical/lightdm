dnl Process this file with autoconf to produce a configure script.

AC_INIT(lightdm, 0.4.4)
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

dnl ###########################################################################
dnl Configurable values
dnl ###########################################################################

DEFAULT_CONFIG_FILE=${sysconfdir}/lightdm/lightdm.conf
AC_ARG_WITH(config-file,
            AS_HELP_STRING(--with-config-file=<file>,
                           Configuration file to load),
    if test x$withval != x; then
        DEFAULT_CONFIG_FILE="$withval"
    fi
)
AC_SUBST(DEFAULT_CONFIG_FILE)

LOG_DIR=${localstatedir}/log/lightdm
AC_ARG_WITH(log-dir,
            AS_HELP_STRING(--with-log-dir=<dir>,
                           Directory to write logs to),
    if test x$withval != x; then
        LOG_DIR="$withval"
    fi
)
AC_SUBST(LOG_DIR)

RUN_DIR=$localstatedir/run/lightdm
AC_ARG_WITH(xauth-dir,
            AS_HELP_STRING(--with-run-dir=<dir>,
                           Directory to put running state in),
    if test x$withval != x; then
        RUN_DIR="$withval"
    fi
)
AC_SUBST(RUN_DIR)

CACHE_DIR=${localstatedir}/cache/lightdm
AC_ARG_WITH(cache-dir,
            AS_HELP_STRING(--with-cache-dir=<dir>,
                           Directory to cache information in),
    if test x$withval != x; then
        CACHE_DIR="$withval"
    fi
)
AC_SUBST(CACHE_DIR)

DBUS_SYS_DIR="${sysconfdir}/dbus-1/system.d"
AC_ARG_WITH(dbus-sys,
            AS_HELP_STRING(--with-dbus-sys=<dir>,
                           Where D-BUS system.d directory is),
    if test x$withval != x ; then
        DBUS_SYS_DIR="$withval"
    fi
)
AC_SUBST(DBUS_SYS_DIR)

XSERVER_BINARY=/usr/bin/X
AC_ARG_WITH(xserver-binary,
            AS_HELP_STRING(--with-xserver-binary=<binary>,
                           X server binary name),
    if test x$withval != x; then
        XSERVER_BINARY="$withval"
    fi
)
AC_SUBST(XSERVER_BINARY)
AC_DEFINE_UNQUOTED(XSERVER_BINARY, "$XSERVER_BINARY", X server binary name)

XSESSIONS_DIR=/usr/share/xsessions
AC_ARG_WITH(xsession-dir,
            AS_HELP_STRING(--with-xsession-dir=<dir>,
                           X session directory),
    if test x$withval != x; then
        XSESSIONS_DIR="$withval"
    fi
)
AC_SUBST(XSESSIONS_DIR)
AC_DEFINE_UNQUOTED(XSESSIONS_DIR, "$XSESSIONS_DIR", X session directory)

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

GREETER_USER=
AC_ARG_WITH(greeter-user,
            AS_HELP_STRING(--with-greeter-user=<username>,
                           User to run greeter as),
    if test x$withval != x; then
        GREETER_USER="$withval"
    fi
)
AC_SUBST(GREETER_USER)
AC_DEFINE_UNQUOTED(GREETER_USER, "$GREETER_USER", User to run greeter as)

DEFAULT_PAM_SERVICE=lightdm
AC_ARG_WITH(default-pam-service,
            AS_HELP_STRING(--with-default-pam-service=<name>,
                           Default PAM service to use),
    if test x$withval != x; then
        DEFAULT_PAM_SERVICE="$withval"
    fi
)
AC_SUBST(DEFAULT_PAM_SERVICE)
AC_DEFINE_UNQUOTED(DEFAULT_PAM_SERVICE, "$DEFAULT_PAM_SERVICE", Default PAM service to use)

DEFAULT_PAM_AUTOLOGIN_SERVICE=lightdm-autologin
AC_ARG_WITH(default-pam-autologin-service,
            AS_HELP_STRING(--with-default-pam-autologin-service=<name>,
                           Default PAM service to use for automatic logins),
    if test x$withval != x; then
        DEFAULT_PAM_AUTOLOGIN_SERVICE="$withval"
    fi
)
AC_SUBST(DEFAULT_PAM_AUTOLOGIN_SERVICE)
AC_DEFINE_UNQUOTED(DEFAULT_PAM_AUTOLOGIN_SERVICE, "$DEFAULT_PAM_AUTOLOGIN_SERVICE", Default PAM service to use for automatic logins)

GREETER_THEME_DIR=${datarootdir}/lightdm/themes
AC_ARG_WITH(greeter-theme-dir,
            AS_HELP_STRING(--with-greeter-theme-dir=<dir>,
                           Directory containing greeter themes),
    if test x$withval != x; then
        GREETER_THEME_DIR="$withval"
    fi
)
AC_SUBST(GREETER_THEME_DIR)

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
        Config file:              $DEFAULT_CONFIG_FILE
        Log directory:            $LOG_DIR        
        D-Bus system directory:   $DBUS_SYS_DIR
        X server binary:          $XSERVER_BINARY
        Run dir:                  $RUN_DIR
        Cache dir:                $CACHE_DIR
        X session dir:            $XSESSIONS_DIR
        Default X session:        $DEFAULT_XSESSION
        Greeter user:             $GREETER_USER
        PAM service:              $DEFAULT_PAM_SERVICE
        PAM service (autologins): $DEFAULT_PAM_AUTOLOGIN_SERVICE
        Greeter theme directory:  $GREETER_THEME_DIR
        Default greeter theme:    $DEFAULT_GREETER_THEME

        Greeters:
        liblightdm-gobject:       $compile_liblightdm_gobject
        GObject introspection:    $found_introspection
        liblightdm-qt:            $compile_liblightdm_qt
        GTK+ Greeter:             $have_gtk
        Vala Greeter:             $have_vala
        Qt Greeter:               $have_qt
"
