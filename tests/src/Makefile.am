noinst_PROGRAMS = dbus-env test-runner X Xvnc test-gobject-greeter test-session test-script-hook guest-account initctl plymouth vnc-client
dist_noinst_SCRIPTS = lightdm-session
noinst_LTLIBRARIES = libsystem.la

libsystem_la_SOURCES = libsystem.c
libsystem_la_CFLAGS = $(GLIB_CFLAGS)
libsystem_la_LIBADD = -ldl

# This is required to have autotools generate a library that is not installed
# but produces a shared library (instead of just a static library)
libsystem_la_LDFLAGS = -rpath /nowhere

if COMPILE_LIBLIGHTDM_QT
noinst_PROGRAMS += test-qt-greeter
endif

dbus_env_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	-DDATADIR=\"$(abs_srcdir)/../data\"
dbus_env_LDADD = \
	$(GLIB_LIBS)

test_runner_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	-DSRCDIR=\"$(abs_top_srcdir)\" \
	-DBUILDDIR=\"$(abs_top_builddir)\" \
	-DDATADIR=\"$(abs_srcdir)/../data\"
test_runner_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_LIBS)

X_SOURCES = X.c x-authority.c x-authority.h x-common.c x-common.h x-server.c x-server.h xdmcp-client.c xdmcp-client.h status.c status.h
X_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
X_LDADD = \
	$(GOBJECT_LIBS) \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS)

Xvnc_SOURCES = Xvnc.c x-authority.c x-authority.h x-common.c x-common.h x-server.c x-server.h status.c status.h
Xvnc_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
Xvnc_LDADD = \
	$(GOBJECT_LIBS) \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS)

test_gobject_greeter_SOURCES = test-gobject-greeter.c status.c status.h
test_gobject_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(XCB_CFLAGS)
test_gobject_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-1 \
	$(GLIB_LIBS) \
	$(GOBJECT_LIBS) \
	$(XCB_LIBS)

guest_account_SOURCES = guest-account.c status.c status.h
guest_account_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS)
guest_account_LDADD = \
	$(GLIB_LIBS)

test_script_hook_SOURCES = test-script-hook.c status.c status.h
test_script_hook_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS)
test_script_hook_LDADD = \
	$(GLIB_LIBS)

test-qt-greeter_moc.cpp: test-qt-greeter.h
	$(MOC) $< -o $@
test_qt_greeter_SOURCES = test-qt-greeter.cpp test-qt-greeter.h status.c status.h
nodist_test_qt_greeter_SOURCES = test-qt-greeter_moc.cpp
test_qt_greeter_CXXFLAGS = \
	-I$(top_srcdir)/liblightdm-qt \
	$(LIBLIGHTDM_QT_CFLAGS) \
	$(XCB_CFLAGS)
test_qt_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-qt \
	-llightdm-qt-2 \
	$(LIBLIGHTDM_QT_LIBS) \
	$(XCB_LIBS)

test_session_SOURCES = test-session.c status.c status.h
test_session_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(XCB_CFLAGS)
test_session_LDADD = \
	$(GLIB_LIBS) \
	$(XCB_LIBS)

initctl_SOURCES = initctl.c status.c status.h
initctl_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS)
initctl_LDADD = \
	$(GLIB_LIBS)

plymouth_SOURCES = plymouth.c status.c status.h
plymouth_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS)
plymouth_LDADD = \
	$(GLIB_LIBS)

vnc_client_SOURCES = vnc-client.c status.c status.h
vnc_client_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS)
vnc_client_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_LIBS)

CLEANFILES = \
	test-qt-greeter_moc.cpp
