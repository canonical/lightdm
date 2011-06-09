noinst_PROGRAMS = test-runner test-xserver test-gobject-greeter test-session

test_runner_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS)
test_runner_LDADD = \
	$(GLIB_LIBS)

test_xserver_SOURCES = test-xserver.c status.c
test_xserver_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS)
test_xserver_LDADD = \
	$(GLIB_LIBS)

test_gobject_greeter_SOURCES = test-gobject-greeter.c status.c
test_gobject_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(XCB_CFLAGS)
test_gobject_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0 \
	$(GLIB_LIBS) \
	$(XCB_LIBS)

test_session_SOURCES = test-session.c status.c
test_session_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(XCB_CFLAGS)
test_session_LDADD = \
	$(GLIB_LIBS) \
	$(XCB_LIBS)
