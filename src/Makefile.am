sbin_PROGRAMS = lightdm

ldm-marshal.c: ldm-marshal.list
	@GLIB_GENMARSHAL@ --prefix=ldm_marshal $(srcdir)/ldm-marshal.list --header --body > ldm-marshal.c

ldm-marshal.h: ldm-marshal.list
	@GLIB_GENMARSHAL@ --prefix=ldm_marshal $(srcdir)/ldm-marshal.list --header > ldm-marshal.h

lightdm_SOURCES = \
	accounts.c \
	accounts.h \
	configuration.c \
	configuration.h \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	display-server.c \
	display-server.h \
	greeter.c \
	greeter.h \
	guest-account.c \
	guest-account.h \
	lightdm.c \
	ldm-marshal.c \
	ldm-marshal.h \
	pam-session.c \
	pam-session.h \
	plymouth.c \
	plymouth.h \
	process.c \
	process.h \
	seat.c \
	seat.h \
	seat-xdmcp-session.c \
	seat-xdmcp-session.h \
	seat-xlocal.c \
	seat-xlocal.h \
	seat-xremote.c \
	seat-xremote.h \
	session.c \
	session.h \
	user.c \
	user.h \
	vt.c \
	vt.h \
	xauthority.c \
	xauthority.h \
	xdisplay.c \
	xdisplay.h \
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
	xserver.h \
	xsession.c \
	xsession.h

BUILT_SOURCES = \
	ldm-marshal.c \
	ldm-marshal.h

lightdm_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	$(WARN_CFLAGS) \
	-DPKGLIBEXEC_DIR=\"$(pkglibexecdir)\" \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\" \
	-DLOG_DIR=\"$(localstatedir)/log/lightdm\" \
	-DRUN_DIR=\"$(localstatedir)/run/lightdm\" \
	-DCACHE_DIR=\"$(localstatedir)/cache/lightdm\" \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\" \
	-DXGREETERS_DIR=\"$(datadir)/xgreeters\"

lightdm_LDADD = \
	$(LIGHTDM_LIBS) \
	-lpam

EXTRA_DIST = ldm-marshal.list \
	display-manager.xml

CLEANFILES = \
	$(BUILT_SOURCES)

DISTCLEANFILES = \
	Makefile.in
