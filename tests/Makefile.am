noinst_PROGRAMS = test-gobject-greeter

test_gobject_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(GOBJECT_CFLAGS)

test_gobject_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0

TESTS = test-autologin
