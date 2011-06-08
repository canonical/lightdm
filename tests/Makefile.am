SUBDIRS = autologin login-gobject

noinst_PROGRAMS = test-gobject-greeter test-xserver

test_gobject_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS)

test_gobject_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0 \
	$(GLIB_LIBS)

test_xserver_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS)

test_xserver_LDADD = \
	$(GLIB_LIBS)
