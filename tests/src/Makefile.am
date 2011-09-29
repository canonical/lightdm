noinst_PROGRAMS = test-runner X test-gobject-greeter test-session test-script-hook guest-account initctl plymouth
noinst_SCRIPTS = lightdm-session
lib_LTLIBRARIES = libsystem.la

libsystem_la_SOURCES = libsystem.c
libsystem_la_CFLAGS = $(GLIB_CFLAGS)
libsystem_la_LIBADD = -ldl

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

X_SOURCES = X.c status.c status.h
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
	moc $< -o $@
test_qt_greeter_SOURCES = test-qt-greeter.cpp test-qt-greeter.h test-qt-greeter_moc.cpp status.c status.h
test_qt_greeter_CXXFLAGS = \
	-I$(top_srcdir)/liblightdm-qt \
	$(LIGHTDM_QT_GREETER_CFLAGS) \
	$(XCB_CFLAGS)
test_qt_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-qt \
	-llightdm-qt-1 \
	$(LIGHTDM_QT_GREETER_LIBS) \
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

CLEANFILES = \
	test-qt-greeter_moc.cpp
