libexec_PROGRAMS = lightdm-example-gtk-greeter

lightdm_example_gtk_greeter_SOURCES = \
	lightdm-example-gtk-greeter.c

lightdm_example_gtk_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(LIGHTDM_EXAMPLE_GTK_GREETER_CFLAGS) \
	$(WARN_CFLAGS)

lightdm_example_gtk_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0 \
	$(LIGHTDM_EXAMPLE_GTK_GREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
