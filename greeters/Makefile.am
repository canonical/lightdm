libexec_PROGRAMS =

if HAVE_GTK_GREETER
libexec_PROGRAMS += ldm-gtk-greeter
endif
if HAVE_WEBKIT_GREETER
libexec_PROGRAMS += ldm-webkit-greeter
endif

LIBGREETER_LIB = $(top_builddir)/libldmgreeter-gobject/libldmgreeter.la
LIBGREETER_CFLAGS = \
	-I$(top_srcdir)/libldmgreeter-gobject
LIBGREETER_LDADD = \
	-L$(top_builddir)/libldmgreeter-gobject \
	-lldmgreeter-gobject

ldm_gtk_greeter_SOURCES = \
	ldm-gtk-greeter.c

ldm_gtk_greeter_CFLAGS = \
	$(LIBGREETER_CFLAGS) \
	$(LIGHTDM_GTK_GREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_GTK_GREETER_BINARY=\"ldm-gtk-greeter\"

ldm_gtk_greeter_LDADD = \
	$(LIBGREETER_LDADD) \
	$(LIGHTDM_GTK_GREETER_LIBS)

ldm_webkit_greeter_SOURCES = \
	ldm-webkit-greeter.c

ldm_webkit_greeter_CFLAGS = \
	$(LIBGREETER_CFLAGS) \
	$(LIGHTDM_WEBKIT_GREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_WEBKIT_GREETER_BINARY=\"ldm-webkit-greeter\"

ldm_webkit_greeter_LDADD = \
	$(LIBGREETER_LDADD) \
	$(LIGHTDM_WEBKIT_GREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
