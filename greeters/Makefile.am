libexec_PROGRAMS =

if HAVE_GTK_GREETER
libexec_PROGRAMS += lightdm-gtk-greeter
endif

LIBLIGHTDM_LIB = $(top_builddir)/liblightdm-gobject/liblightdm-gobject.la
LIBLIGHTDM_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject
LIBLIGHTDM_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0

lightdm_gtk_greeter_SOURCES = \
	lightdm-gtk-greeter.c

lightdm_gtk_greeter_CFLAGS = \
	$(LIBLIGHTDM_CFLAGS) \
	$(LIGHTDM_GTK_GREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_GTK_GREETER_BINARY=\"lightdm-gtk-greeter\"

lightdm_gtk_greeter_LDADD = \
	$(LIBLIGHTDM_LDADD) \
	$(LIGHTDM_GTK_GREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
