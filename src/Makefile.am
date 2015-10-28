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
	console-kit.c \
	console-kit.h \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	display-server.c \
	display-server.h \
	dmrc.c \
	dmrc.h \
	greeter.c \
	greeter.h \
	guest-account.c \
	guest-account.h \
	lightdm.c \
	ldm-marshal.c \
	ldm-marshal.h \
	log-file.c \
	log-file.h \
	plymouth.c \
	plymouth.h \
	privileges.c \
	privileges.h \
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
	seat-xvnc.c \
	seat-xvnc.h \
	session.c \
	session.h \
	session-child.c \
	session-child.h \
	vnc-server.c \
	vnc-server.h \
	vt.c \
	vt.h \
	xauthority.c \
	xauthority.h \
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
	xserver-xvnc.c \
	xserver-xvnc.h \
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
	-DSBIN_DIR=\"$(sbindir)\" \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\" \
	-DLOG_DIR=\"$(localstatedir)/log/lightdm\" \
	-DRUN_DIR=\"$(localstatedir)/run/lightdm\" \
	-DCACHE_DIR=\"$(localstatedir)/cache/lightdm\" \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\" \
	-DSYSTEM_CONFIG_DIR=\"$(pkgdatadir)/lightdm.conf.d\" \
	-DXGREETERS_DIR=\"$(datadir)/xgreeters\"

lightdm_LDADD = \
	$(LIGHTDM_LIBS) \
	-lpam

pkglibexec_PROGRAMS = lightdm-guest-session-wrapper

lightdm_guest_session_wrapper_SOURCES = lightdm-guest-session-wrapper.c

lightdm_guest_session_wrapper_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	$(WARN_CFLAGS)

EXTRA_DIST = ldm-marshal.list \
	display-manager.xml

CLEANFILES = \
	$(BUILT_SOURCES)

DISTCLEANFILES = \
	Makefile.in
