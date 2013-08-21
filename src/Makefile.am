sbin_PROGRAMS = lightdm

lightdm_SOURCES = \
	accounts.c \
	accounts.h \
	configuration.c \
	configuration.h \
	console-kit.c \
	console-kit.h \
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
	logger.c \
	logger.h \
	login1.c \
	login1.h \
	mir-server.c \
	mir-server.h \
	plymouth.c \
	plymouth.h \
	privileges.c \
	privileges.h \
	process.c \
	process.h \
	seat.c \
	seat.h \
	seat-unity.c \
	seat-unity.h \
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
	session-config.c \
	session-config.h \
	vnc-server.c \
	vnc-server.h \
	vt.c \
	vt.h \
	x-authority.c \
	x-authority.h \
	x-server-local.c \
	x-server-local.h \
	x-server-remote.c \
	x-server-remote.h \
	x-server-xvnc.c \
	x-server-xvnc.h \
	x-server.c \
	x-server.h \
	xdmcp-protocol.c \
	xdmcp-protocol.h \
	xdmcp-server.c \
	xdmcp-server.h \
	xdmcp-session.c \
	xdmcp-session.h \
	xdmcp-session-private.h

lightdm_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	$(WARN_CFLAGS) \
	-DPKGLIBEXEC_DIR=\"$(pkglibexecdir)\" \
	-DSBIN_DIR=\"$(sbindir)\" \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\" \
	-DLOG_DIR=\"$(localstatedir)/log/lightdm\" \
	-DRUN_DIR=\"$(localstatedir)/run/lightdm\" \
	-DCACHE_DIR=\"$(localstatedir)/cache/lightdm\" \
	-DSESSIONS_DIR=\"$(pkgdatadir)/sessions:$(datadir)/xsessions\" \
	-DREMOTE_SESSIONS_DIR=\"$(pkgdatadir)/remote-sessions\" \
	-DGREETERS_DIR=\"$(pkgdatadir)/greeters:$(datadir)/xgreeters\"

lightdm_LDADD = \
	$(LIGHTDM_LIBS) \
	-lgcrypt \
	-lpam

pkglibexec_PROGRAMS = lightdm-guest-session-wrapper

lightdm_guest_session_wrapper_SOURCES = lightdm-guest-session-wrapper.c

lightdm_guest_session_wrapper_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	$(WARN_CFLAGS)

EXTRA_DIST = \
	display-manager.xml

DISTCLEANFILES = \
	Makefile.in
