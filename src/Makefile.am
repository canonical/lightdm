bin_PROGRAMS = lightdm

ldm-marshal.c: ldm-marshal.list
	@GLIB_GENMARSHAL@ --prefix=ldm_marshal $(srcdir)/ldm-marshal.list --header --body > ldm-marshal.c

ldm-marshal.h: ldm-marshal.list
	@GLIB_GENMARSHAL@ --prefix=ldm_marshal $(srcdir)/ldm-marshal.list --header > ldm-marshal.h

lightdm_SOURCES = \
	child-process.c \
	child-process.h \
	configuration.c \
	configuration.h \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	dmrc.c \
	dmrc.h \
	greeter.c \
	greeter.h \
	greeter-protocol.h \
	guest-account.c \
	guest-account.h \
	lightdm.c \
	ldm-marshal.c \
	ldm-marshal.h \
	pam-session.c \
	pam-session.h \
	plymouth.c \
	plymouth.h \
	seat.c \
	seat.h \
	seat-local.c \
	seat-local.h \
	seat-xdmcp-client.c \
	seat-xdmcp-client.h \
	seat-xdmcp-session.c \
	seat-xdmcp-session.h \
	session.c \
	session.h \
	theme.c \
	theme.h \
	user.c \
	user.h \
	vt.c \
	vt.h \
	xauth.c \
	xauth.h \
	xdmcp-protocol.c \
	xdmcp-protocol.h \
	xdmcp-server.c \
	xdmcp-server.h \
	xdmcp-session.c \
	xdmcp-session.h \
	xdmcp-session-private.h \
	xserver-local.c \
	xserver-local.h \
	xserver-remote.c \
	xserver-remote.h \
	xserver.c \
	xserver.h

BUILT_SOURCES = \
	ldm-marshal.c \
	ldm-marshal.h

lightdm_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	$(WARN_CFLAGS) \
	-DLIGHTDM_BINARY=\"lightdm\" \
	-DDEFAULT_CONFIG_FILE=\"$(sysconfdir)/lightdm/lightdm.conf\" \
	-DRUN_DIR=\"$(localstatedir)/run/lightdm\" \
	-DCACHE_DIR=\"$(localstatedir)/cache/lightdm\" \
	-DLOG_DIR=\"$(localstatedir)/log/lightdm\" \
	-DGREETER_THEME_ENGINE_DIR=\"$(GREETER_THEME_ENGINE_DIR)\" \
	-DGREETER_THEME_DIR=\"$(GREETER_THEME_DIR)\" \
	-DXSESSIONS_DIR=\"/usr/share/xsessions\"

lightdm_LDADD = \
	$(LIGHTDM_LIBS) \
	-lpam

EXTRA_DIST = ldm-marshal.list \
	display-manager.xml

CLEANFILES = \
	$(BUILT_SOURCES)

DISTCLEANFILES = \
	Makefile.in
