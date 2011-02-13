bin_PROGRAMS = lightdm

ldm-marshal.c: ldm-marshal.list
	@GLIB_GENMARSHAL@ --prefix=ldm_marshal $(srcdir)/ldm-marshal.list --header --body > ldm-marshal.c

ldm-marshal.h: ldm-marshal.list
	@GLIB_GENMARSHAL@ --prefix=ldm_marshal $(srcdir)/ldm-marshal.list --header > ldm-marshal.h

display-glue.h: $(srcdir)/display.xml
	$(DBUSBINDINGTOOL) --prefix=display --mode=glib-server --output=$@ $<
display-manager-glue.h: $(srcdir)/display-manager.xml
	$(DBUSBINDINGTOOL) --prefix=display_manager --mode=glib-server --output=$@ $<
user-manager-glue.h: $(srcdir)/user-manager.xml
	$(DBUSBINDINGTOOL) --prefix=user_manager --mode=glib-server --output=$@ $<

lightdm_SOURCES = \
	child-process.c \
	child-process.h \
	ck-connector.c \
	ck-connector.h \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	lightdm.c \
	ldm-marshal.c \
	ldm-marshal.h \
	pam-session.c \
	pam-session.h \
	session.c \
	session.h \
	theme.c \
	theme.h \
	user-manager.c \
	user-manager.h \
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
	display-glue.h \
	display-manager-glue.h \
	user-manager-glue.h \
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
	display.xml \
	user-manager.xml

CLEANFILES = \
	$(BUILT_SOURCES)

DISTCLEANFILES = \
	Makefile.in
