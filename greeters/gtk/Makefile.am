libexec_PROGRAMS = lightdm-gtk-greeter

lightdm_gtk_greeter_SOURCES = \
	lightdm-gtk-greeter.c

lightdm_gtk_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(LIGHTDM_GTK_GREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_GTK_GREETER_BINARY=\"lightdm-gtk-greeter\"

lightdm_gtk_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0 \
	$(LIGHTDM_GTK_GREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
