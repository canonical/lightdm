libexec_PROGRAMS = lightdm-example-vala-gtk-greeter

lightdm_example_vala_gtk_greeter_SOURCES = \
	lightdm-example-vala-gtk-greeter.vala

lightdm_example_vala_gtk_greeter_VALAFLAGS = \
	--pkg gtk+-2.0 \
	--vapidir $(top_builddir)/liblightdm-gobject \
	--pkg lightdm-0

lightdm_example_vala_gtk_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(LIGHTDM_EXAMPLE_GTK_GREETER_CFLAGS) \
	$(WARN_CFLAGS)

lightdm_example_vala_gtk_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0 \
	$(LIGHTDM_EXAMPLE_GTK_GREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
