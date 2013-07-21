noinst_PROGRAMS = dbus-env \
                  initctl \
                  plymouth \
                  test-gobject-greeter \
                  test-greeter-wrapper \
                  test-runner \
                  test-script-hook \
                  test-session \
                  guest-account \
                  unity-system-compositor \
                  vnc-client \
                  X \
                  Xvnc
dist_noinst_SCRIPTS = lightdm-session \
                      test-python-greeter
noinst_LTLIBRARIES = libsystem.la

libsystem_la_SOURCES = libsystem.c
libsystem_la_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	-DSRCDIR=\"$(abs_top_srcdir)\" \
	-DBUILDDIR=\"$(abs_top_builddir)\" \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	-DLOCALSTATEDIR=\"$(localstatedir)\" \
	-DDATADIR=\"$(datadir)\" \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\"
libsystem_la_LIBADD = -ldl $(GLIB_LIBS) $(GIO_UNIX_LIBS)

# This is required to have autotools generate a library that is not installed
# but produces a shared library (instead of just a static library)
libsystem_la_LDFLAGS = -rpath /nowhere

if COMPILE_LIBLIGHTDM_QT4
noinst_PROGRAMS += test-qt4-greeter
endif

if COMPILE_LIBLIGHTDM_QT5
noinst_PROGRAMS += test-qt5-greeter
endif

dbus_env_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	-DDATADIR=\"$(abs_srcdir)/../data\"
dbus_env_LDADD = \
	$(GLIB_LIBS)

test_runner_SOURCES = test-runner.c
test_runner_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	-DSRCDIR=\"$(abs_top_srcdir)\" \
	-DBUILDDIR=\"$(abs_top_builddir)\" \
	-DDATADIR=\"$(abs_srcdir)/../data\"
test_runner_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS)

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

test_greeter_wrapper_SOURCES = test-greeter-wrapper.c status.c status.h
test_greeter_wrapper_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
test_greeter_wrapper_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_UNIX_LIBS)

test_gobject_greeter_SOURCES = test-gobject-greeter.c status.c status.h
test_gobject_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(XCB_CFLAGS)
test_gobject_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-1 \
	$(GLIB_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(XCB_LIBS)    

guest_account_SOURCES = guest-account.c status.c status.h
guest_account_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
guest_account_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_UNIX_LIBS)

test_script_hook_SOURCES = test-script-hook.c status.c status.h
test_script_hook_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
test_script_hook_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_UNIX_LIBS)

test-qt4-greeter_moc4.cpp: test-qt-greeter.h
	$(am__v_MOC4_$(V)) $(MOC4) $< -o $@
test-qt5-greeter_moc5.cpp: test-qt-greeter.h
	$(am__v_MOC5_$(V)) $(MOC5) $< -o $@
common_qt_sources = test-qt-greeter.cpp test-qt-greeter.h status.c status.h
test_qt4_greeter_SOURCES = $(common_qt_sources)
test_qt5_greeter_SOURCES = $(common_qt_sources)
nodist_test_qt4_greeter_SOURCES = test-qt4-greeter_moc4.cpp
nodist_test_qt5_greeter_SOURCES = test-qt5-greeter_moc5.cpp
common_qt_cflags = \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(XCB_CFLAGS)
test_qt4_greeter_CFLAGS = $(common_qt_cflags)
test_qt5_greeter_CFLAGS = $(common_qt_cflags)
common_qt_cxxflags = \
	-fPIC \
	-DQT_DISABLE_DEPRECATED_BEFORE="QT_VERSION_CHECK(4, 0, 0)" \
	-I$(top_srcdir)/liblightdm-qt \
	$(GLIB_CFLAGS)
test_qt4_greeter_CXXFLAGS = \
	$(common_qt_cxxflags) \
	$(LIBLIGHTDM_QT4_CFLAGS)
test_qt5_greeter_CXXFLAGS = \
	$(common_qt_cxxflags) \
	$(LIBLIGHTDM_QT5_CFLAGS)
common_qt_ldadd = \
	-L$(top_builddir)/liblightdm-qt \
	$(GLIB_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(XCB_LIBS)
test_qt4_greeter_LDADD = \
	$(common_qt_ldadd) \
	-llightdm-qt-3 \
	$(LIBLIGHTDM_QT4_LIBS)
test_qt5_greeter_LDADD = \
	$(common_qt_ldadd) \
	-llightdm-qt5-3 \
	$(LIBLIGHTDM_QT5_LIBS)

test_session_SOURCES = test-session.c status.c status.h
test_session_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(XCB_CFLAGS)
test_session_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(XCB_LIBS)

initctl_SOURCES = initctl.c
initctl_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
initctl_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_UNIX_LIBS)

plymouth_SOURCES = plymouth.c status.c status.h
plymouth_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
plymouth_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_UNIX_LIBS)

unity_system_compositor_SOURCES = unity-system-compositor.c status.c status.h
unity_system_compositor_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
unity_system_compositor_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS)

vnc_client_SOURCES = vnc-client.c status.c status.h
vnc_client_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS)
vnc_client_LDADD = \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS)

CLEANFILES = \
	test-qt4-greeter_moc4.cpp \
	test-qt5-greeter_moc5.cpp

# Support pretty printing MOC
AM_V_MOC4 = $(am__v_MOC4_$(V))
am__v_MOC4_ = $(am__v_MOC4_$(AM_DEFAULT_VERBOSITY))
am__v_MOC4_0 = @echo "  MOC4    " $@;
%_moc4.cpp: QLightDM/%.h
	$(AM_V_MOC4) $(MOC4) $< -o $@

AM_V_MOC5 = $(am__v_MOC5_$(V))
am__v_MOC5_ = $(am__v_MOC5_$(AM_DEFAULT_VERBOSITY))
am__v_MOC5_0 = @echo "  MOC5    " $@;
%_moc5.cpp: QLightDM/%.h
	$(AM_V_MOC5) $(MOC5) $< -o $@
