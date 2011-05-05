bin_PROGRAMS = lightdm

ldm-marshal.c: ldm-marshal.list
	@GLIB_GENMARSHAL@ --prefix=ldm_marshal $(srcdir)/ldm-marshal.list --header --body > ldm-marshal.c

ldm-marshal.h: ldm-marshal.list
	@GLIB_GENMARSHAL@ --prefix=ldm_marshal $(srcdir)/ldm-marshal.list --header > ldm-marshal.h

lightdm_SOURCES = \
	child-process.c \
	child-process.h \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	greeter.c \
	greeter.h \
	greeter-protocol.h \
	lightdm.c \
	ldm-marshal.c \
	ldm-marshal.h \
	pam-session.c \
	pam-session.h \
	session.c \
	session.h \
	theme.c \
	theme.h \
	xauth.c \
	xauth.h \
	xdmcp-protocol.c \
	xdmcp-protocol.h \
	xdmcp-server.c \
	xdmcp-server.h \
	xdmcp-session.c \
	xdmcp-session.h \
	xdmcp-session-private.h \
	xserver.c \
	xserver.h

BUILT_SOURCES = \
	ldm-marshal.c \
	ldm-marshal.h

lightdm_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	$(WARN_CFLAGS) \
	-DCONFIG_FILE=\"$(CONFIG_FILE)\" \
	-DLOG_DIR=\"$(LOG_DIR)\" \
	-DTHEME_ENGINE_DIR=\"$(libexecdir)\" \
	-DXAUTH_DIR=\"$(XAUTH_DIR)\" \
	-DCACHE_DIR=\"$(CACHE_DIR)\" \
	-DTHEME_DIR=\"$(THEME_DIR)\" \
	-DLIGHTDM_BINARY=\"lightdm\"

lightdm_LDADD = \
	$(LIGHTDM_LIBS) \
	-lpam

EXTRA_DIST = ldm-marshal.list \
	display-manager.xml \
	user-manager.xml

CLEANFILES = \
	$(BUILT_SOURCES)

DISTCLEANFILES = \
	Makefile.in
