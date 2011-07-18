bin_PROGRAMS = lightdm-gtk-greeter

uidir = $(datadir)/lightdm-gtk-greeter
dist_ui_DATA = greeter.ui

xsessiondir = $(datadir)/xsessions
dist_xsession_DATA = lightdm-gtk-greeter.desktop

lightdm_gtk_greeter_SOURCES = \
	lightdm-gtk-greeter.c

lightdm_gtk_greeter_CFLAGS = \
	-DUI_DIR=\""$(uidir)"\" \
	-I$(top_srcdir)/liblightdm-gobject \
	$(LIGHTDM_GTK_GREETER_CFLAGS) \
	$(WARN_CFLAGS)

lightdm_gtk_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0 \
	$(LIGHTDM_GTK_GREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
