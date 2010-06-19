libexec_PROGRAMS = ldm-gtk-greeter ldm-webkit-greeter

LIBGREETER_LIB = $(top_builddir)/libldmgreeter/libldmgreeter.la
LIBGREETER_CFLAGS = \
	-I$(top_srcdir)/libldmgreeter
LIBGREETER_LDADD = \
	-L$(top_srcdir)/libldmgreeter \
	-lldmgreeter

ldm_gtk_greeter_SOURCES = \
	ldm-gtk-greeter.c

ldm_gtk_greeter_CFLAGS = \
	$(LIBGREETER_CFLAGS) \
	$(LIGHTDM_GREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_GREETER_BINARY=\"ldm-gtk-greeter\"

ldm_gtk_greeter_LDADD = \
	$(LIBGREETER_LDADD) \
	$(LIGHTDM_GREETER_LIBS)

ldm_webkit_greeter_SOURCES = \
	ldm-webkit-greeter.c

ldm_webkit_greeter_CFLAGS = \
	$(LIBGREETER_CFLAGS) \
	$(LIGHTDM_WKGREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_WKGREETER_BINARY=\"ldm-webkit-greeter\"

ldm_webkit_greeter_LDADD = \
	$(LIBGREETER_LDADD) \
	$(LIGHTDM_WKGREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
