sbin_PROGRAMS = lightdm
bin_PROGRAMS = dm-tool

lightdm_SOURCES = \
	accounts.c \
	accounts.h \
	console-kit.c \
	console-kit.h \
	display-manager.c \
	display-manager.h \
	display-manager-service.c \
	display-manager-service.h \
	display-server.c \
	display-server.h \
	greeter.c \
	greeter.h \
	greeter-session.c \
	greeter-session.h \
	greeter-socket.c \
	greeter-socket.h \
	guest-account.c \
	guest-account.h \
	lightdm.c \
	logger.c \
	logger.h \
	login1.c \
	login1.h \
	log-file.c \
	log-file.h \
	plymouth.c \
	plymouth.h \
	process.c \
	process.h \
	seat.c \
	seat.h \
	seat-local.c \
	seat-local.h \
	seat-xdmcp-session.c \
	seat-xdmcp-session.h \
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
	vnc-server.c \
	vnc-server.h \
	vt.c \
	vt.h \
	wayland-session.c \
	wayland-session.h \
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
	xdmcp-session.h

lightdm_CFLAGS = \
	$(WARN_CFLAGS) \
	$(LIGHTDM_CFLAGS) \
	-I"$(top_srcdir)/common" \
	-DSBIN_DIR=\"$(sbindir)\" \
	-DUSERS_DIR=\"$(localstatedir)/lib/lightdm-data\" \
	-DLOG_DIR=\"$(localstatedir)/log/lightdm\" \
	-DRUN_DIR=\"$(localstatedir)/run/lightdm\" \
	-DCACHE_DIR=\"$(localstatedir)/cache/lightdm\" \
	-DSESSIONS_DIR=\"$(pkgdatadir)/sessions:$(datadir)/xsessions:$(datadir)/wayland-sessions\" \
	-DWAYLAND_SESSIONS_DIR=\"$(datadir)/wayland-sessions\" \
	-DREMOTE_SESSIONS_DIR=\"$(pkgdatadir)/remote-sessions\"

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
