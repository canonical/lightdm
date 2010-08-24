dnl Process this file with autoconf to produce a configure script.

AC_INIT(lightdm, 0.1.1)
AC_CONFIG_MACRO_DIR(m4)
AC_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE
LT_INIT
AM_PROG_CC_C_O
AC_PROG_LIBTOOL
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])

GOBJECT_INTROSPECTION_CHECK(0.6.7)

GNOME_COMPILE_WARNINGS(maximum)

dnl ###########################################################################
dnl Dependencies
dnl ###########################################################################

AC_CHECK_HEADERS(security/pam_appl.h, [], AC_MSG_ERROR(PAM not found))

PKG_CHECK_MODULES(LIGHTDM, [
    glib-2.0
    gio-2.0
    gio-unix-2.0
    gobject-2.0
    dbus-glib-1
    xcb
    xdmcp
])

AC_ARG_ENABLE(console-kit,
              AC_HELP_STRING(--enable-console-kit=[no/auto/yes], Enable Console Kit),
              enable_console_kit=$enableval,
              enable_console_kit="auto")

have_console_kit=no
case "$enable_console_kit" in
    "auto")
        PKG_CHECK_MODULES(CONSOLE_KIT,
                          ck-connector,
                          have_console_kit=yes,
                          have_console_kit=no)
        ;;
    "yes")
        PKG_CHECK_MODULES(CONSOLE_KIT, ck-connector)
        have_console_kit=yes;
        ;;
esac

if test $have_console_kit = yes; then
    AC_DEFINE(HAVE_CONSOLE_KIT, 1, Define if using Console Kit)
fi

PKG_CHECK_MODULES(LIBLDMGREETER, [
    glib-2.0
    gio-unix-2.0
    gobject-2.0
    dbus-glib-1
    libxklavier
    x11
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

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(DBUSBINDINGTOOL, dbus-binding-tool)
AC_SUBST(DBUSBINDINGTOOL)

dnl ###########################################################################
dnl Configurable values
dnl ###########################################################################

CONFIG_FILE=${sysconfdir}/lightdm.conf
AC_ARG_WITH(config-file,
            AS_HELP_STRING(--with-config-file=<file>,
                           Configuration file to load),
    if test x$withval != x; then
        CONFIG_FILE="$withval"
    fi
)
AC_SUBST(CONFIG_FILE)

LOG_DIR=${localstatedir}/log/lightdm
AC_ARG_WITH(log-dir,
            AS_HELP_STRING(--with-log-dir=<file>,
                           Directory to write logs to),
    if test x$withval != x; then
        LOG_DIR="$withval"
    fi
)
AC_SUBST(LOG_DIR)

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

XAUTH_DIR=$localstatedir/run/lightdm/authority
AC_ARG_WITH(xauth-dir,
            AS_HELP_STRING(--with-xauth-dir=<dir>,
                           X server authorization directory),
    if test x$withval != x; then
        XAUTH_DIR="$withval"
    fi
)
AC_SUBST(XAUTH_DIR)

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

DEFAULT_SESSION=gnome
AC_ARG_WITH(default-session,
            AS_HELP_STRING(--with-default-session=<name>,
                           Default session to launch),
    if test x$withval != x; then
        DEFAULT_SESSION="$withval"
    fi
)
AC_SUBST(DEFAULT_SESSION)
AC_DEFINE_UNQUOTED(DEFAULT_SESSION, "$DEFAULT_SESSION", Default session)

GREETER_USER=gdm
AC_ARG_WITH(greeter-user,
            AS_HELP_STRING(--with-greeter-user=<username>,
                           User to run greeter as),
    if test x$withval != x; then
        GREETER_USER="$withval"
    fi
)
AC_SUBST(GREETER_USER)
AC_DEFINE_UNQUOTED(GREETER_USER, "$GREETER_USER", User to run greeter as)

THEME_DIR=${prefix}/share/lightdm/themes
AC_ARG_WITH(theme-dir,
            AS_HELP_STRING(--with-theme-dir=<dir>,
                           Directory containing greeter themes),
    if test x$withval != x; then
        THEME_DIR="$withval"
    fi
)
AC_SUBST(THEME_DIR)
AC_DEFINE_UNQUOTED(THEME_DIR, "$THEME_DIR", Directory containing greeter themes)

GREETER_THEME=gnome
AC_ARG_WITH(theme,
            AS_HELP_STRING(--with-theme=<theme>,
                           Default theme),
    if test x$withval != x; then
        GREETER_THEME="$withval"
    fi
)
AC_SUBST(GREETER_THEME)
AC_DEFINE_UNQUOTED(GREETER_THEME, "$GREETER_THEME", Default theme)

dnl ###########################################################################
dnl Documentation
dnl ###########################################################################

GNOME_DOC_INIT
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

AC_OUTPUT([
Makefile
data/org.lightdm.LightDisplayManager.conf
data/Makefile
data/init/Makefile
doc/Makefile
greeters/Makefile
libldmgreeter/libldmgreeter-1.pc
libldmgreeter/Makefile
po/Makefile.in
src/Makefile
themes/Makefile
themes/gnome/Makefile
themes/webkit/Makefile
])

dnl ###########################################################################
dnl Summary
dnl ###########################################################################

echo "
                    Light Display Manager $VERSION
                    ===========================

        prefix:                   $prefix
        Config file:              $CONFIG_FILE
        Log directory:            $LOG_DIR        
        D-Bus system directory:   $DBUS_SYS_DIR
        X server binary:          $XSERVER_BINARY
        X authorization dir:      $XAUTH_DIR
        XSessions dir:            $XSESSIONS_DIR
        Default session:          $DEFAULT_SESSION
        Console Kit:              $have_console_kit
        Greeter user:             $GREETER_USER
        Theme directory:          $THEME_DIR
        Theme:                    $GREETER_THEME
"
