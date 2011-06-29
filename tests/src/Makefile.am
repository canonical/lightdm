noinst_PROGRAMS = test-runner test-xserver test-gobject-greeter test-session
dist_noinst_SCRIPTS = test-setup-guest test-cleanup-guest

test_runner_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	-DSRCDIR=\"$(abs_top_srcdir)\" \
	-DBUILDDIR=\"$(abs_top_builddir)\"
test_runner_LDADD = \
	$(GLIB_LIBS)

test_xserver_SOURCES = test-xserver.c status.c status.h
test_xserver_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GLIB_CFLAGS)
test_xserver_LDADD = \
	$(GOBJECT_LIBS) \
	$(GLIB_LIBS)

test_gobject_greeter_SOURCES = test-gobject-greeter.c status.c status.h
test_gobject_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(XCB_CFLAGS)
test_gobject_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0 \
	$(GLIB_LIBS) \
	$(GOBJECT_LIBS) \
	$(XCB_LIBS)

test_session_SOURCES = test-session.c status.c status.h
test_session_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(XCB_CFLAGS)
test_session_LDADD = \
	$(GLIB_LIBS) \
	$(XCB_LIBS)
