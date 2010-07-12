bin_PROGRAMS = lightdm

display-glue.h: $(srcdir)/display.xml
	$(DBUSBINDINGTOOL) --prefix=display --mode=glib-server --output=$@ $^
display-manager-glue.h: $(srcdir)/display-manager.xml
	$(DBUSBINDINGTOOL) --prefix=display_manager --mode=glib-server --output=$@ $^
session-manager-glue.h: $(srcdir)/session-manager.xml
	$(DBUSBINDINGTOOL) --prefix=session_manager --mode=glib-server --output=$@ $^
user-manager-glue.h: $(srcdir)/user-manager.xml
	$(DBUSBINDINGTOOL) --prefix=user_manager --mode=glib-server --output=$@ $^

lightdm_SOURCES = \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	lightdm.c \
	pam-session.c \
	pam-session.h \
	session.c \
	session.h \
	session-manager.c \
	session-manager.h \
	theme.c \
	theme.h \
	user-manager.c \
	user-manager.h \
	xserver.c \
	xserver.h

BUILT_SOURCES = \
	display-glue.h \
	display-manager-glue.h \
	session-manager-glue.h \
	user-manager-glue.h

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

EXTRA_DIST = display-manager.xml \
	display.xml \
	session-manager.xml \
	user-manager.xml

DISTCLEANFILES = \
	$(BUILT_SOURCES) \
	Makefile.in
