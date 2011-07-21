noinst_PROGRAMS = test-runner test-xserver test-gobject-greeter test-session plymouth
dist_noinst_SCRIPTS = test-setup-guest test-cleanup-guest

if COMPILE_LIBLIGHTDM_QT
noinst_PROGRAMS += test-qt-greeter
endif

test_runner_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	-DSRCDIR=\"$(abs_top_srcdir)\" \
	-DBUILDDIR=\"$(abs_top_builddir)\"
test_runner_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_LIBS)

test_xserver_SOURCES = test-xserver.c status.c status.h
test_xserver_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
test_xserver_LDADD = \
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

test-qt-greeter_moc.cpp: test-qt-greeter.h
	moc $< -o $@
test_qt_greeter_SOURCES = test-qt-greeter.cpp test-qt-greeter_moc.cpp status.c status.h
test_qt_greeter_CXXFLAGS = \
	-I$(top_srcdir)/liblightdm-qt \
	$(LIGHTDM_QT_GREETER_CFLAGS)
test_qt_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-qt \
	-llightdm-qt-1 \
	$(LIGHTDM_QT_GREETER_LIBS)

test_session_SOURCES = test-session.c status.c status.h
test_session_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(XCB_CFLAGS)
test_session_LDADD = \
	$(GLIB_LIBS) \
	$(XCB_LIBS)

plymouth_SOURCES = plymouth.c status.c status.h
plymouth_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS)
plymouth_LDADD = \
	$(GLIB_LIBS)

CLEANFILES = \
	test-qt-greeter_moc.cpp
