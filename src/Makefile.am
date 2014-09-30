sbin_PROGRAMS = lightdm
bin_PROGRAMS = dm-tool

lightdm_SOURCES = \
	accounts.c \
	accounts.h \
	display-manager.c \
	display-manager.h \
	display-server.c \
	display-server.h \
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
	shared-data-manager.c \
	shared-data-manager.h \
	unity-system-compositor.c \
	unity-system-compositor.h \
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
	$(WARN_CFLAGS) \
	$(LIGHTDM_CFLAGS) \
	-I"$(top_srcdir)/common" \
	-DSBIN_DIR=\"$(sbindir)\" \
	-DUSERS_DIR=\"$(localstatedir)/lib/lightdm-data\" \
	-DLOG_DIR=\"$(localstatedir)/log/lightdm\" \
	-DRUN_DIR=\"$(localstatedir)/run/lightdm\" \
	-DCACHE_DIR=\"$(localstatedir)/cache/lightdm\" \
	-DSESSIONS_DIR=\"$(pkgdatadir)/sessions:$(datadir)/xsessions\" \
	-DREMOTE_SESSIONS_DIR=\"$(pkgdatadir)/remote-sessions\" \
	-DGREETERS_DIR=\"$(pkgdatadir)/greeters:$(datadir)/xgreeters\"

lightdm_LDADD = \
	$(LIGHTDM_LIBS) \
	$(top_builddir)/common/libcommon.la \
	-lgcrypt \
	-lpam

dm_tool_SOURCES = \
	dm-tool.c

dm_tool_CFLAGS = \
	$(WARN_CFLAGS) \
	$(LIGHTDM_CFLAGS) \
	-DLOCALE_DIR=\"$(datadir)/locale\"

dm_tool_LDADD = \
	$(LIGHTDM_LIBS)

libexec_PROGRAMS = lightdm-guest-session

lightdm_guest_session_SOURCES = lightdm-guest-session.c

lightdm_guest_session_CFLAGS = \
	$(WARN_CFLAGS) \
	$(LIGHTDM_CFLAGS)

EXTRA_DIST = \
	display-manager.xml

DISTCLEANFILES = \
	Makefile.in
