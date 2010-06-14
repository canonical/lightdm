bin_PROGRAMS = lightdm

libexec_PROGRAMS = ldm-gtk-greeter ldm-webkit-greeter

display-glue.h: display.xml
	$(DBUSBINDINGTOOL) --prefix=display --mode=glib-server --output=$@ $^
display-manager-glue.h: display-manager.xml
	$(DBUSBINDINGTOOL) --prefix=display_manager --mode=glib-server --output=$@ $^
session-manager-glue.h: session-manager.xml
	$(DBUSBINDINGTOOL) --prefix=session_manager --mode=glib-server --output=$@ $^
user-manager-glue.h: user-manager.xml
	$(DBUSBINDINGTOOL) --prefix=user_manager --mode=glib-server --output=$@ $^

lightdm_SOURCES = \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	lightdm.c \
	pam-session.c \
	pam-session.h \
	session-manager.c \
	session-manager.h \
	user-manager.c \
	user-manager.h

ldm_gtk_greeter_SOURCES = \
	greeter.c \
	greeter.h \
	ldm-gtk-greeter.c

ldm_webkit_greeter_SOURCES = \
	greeter.c \
	greeter.h \
	ldm-webkit-greeter.c

BUILT_SOURCES = \
	display-glue.h \
	display-manager-glue.h \
	session-manager-glue.h \
	user-manager-glue.h

# FIXME: Make these configurable from configure.ac
lightdm_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DXSERVER_BINARY=\"$(XSERVER_BINARY)\" \
	-DXSESSIONS_DIR=\"$(XSESSION_DIR)\" \
	-DDEFAULT_SESSION=\"$(DEFAULT_SESSION)\" \
	-DGREETER_USER=\"$(GREETER_USER)\" \
	-DTHEME_DIR=\"$(THEME_DIR)\" \
	-DTHEME_ENGINE_DIR=\"$(libexecdir)\" \
	-DGREETER_THEME=\"$(GREETER_THEME)\" \
	-DCONFIG_FILE=\"$(CONFIG_FILE)\" \
	-DLIGHTDM_BINARY=\"lightdm\"

lightdm_LDADD = \
	$(LIGHTDM_LIBS) \
	-lpam

ldm_gtk_greeter_CFLAGS = \
	$(LIGHTDM_GREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_GREETER_BINARY=\"ldm-gtk-greeter\"

ldm_gtk_greeter_LDADD = \
	$(LIGHTDM_GREETER_LIBS)

ldm_webkit_greeter_CFLAGS = \
	$(LIGHTDM_WKGREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_WKGREETER_BINARY=\"ldm-webkit-greeter\"

ldm_webkit_greeter_LDADD = \
	$(LIGHTDM_WKGREETER_LIBS)

DISTCLEANFILES = \
	$(BUILT_SOURCES) \
	Makefile.in
