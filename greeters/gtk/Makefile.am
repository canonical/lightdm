bin_PROGRAMS = lightdm-gtk-greeter

greeterdatadir = $(datadir)/lightdm-gtk-greeter
dist_greeterdata_DATA = greeter.ui

xgreetersdir = $(datadir)/xgreeters
dist_xgreeters_DATA = lightdm-gtk-greeter.desktop

icondir = $(greeterdatadir)/hicolor/scalable/apps
dist_icon_DATA = accessibility.svg system-shutdown.svg

configdir = $(sysconfdir)/lightdm
dist_config_DATA = lightdm-gtk-greeter.conf

lightdm_gtk_greeter_SOURCES = \
	lightdm-gtk-greeter.c

lightdm_gtk_greeter_CFLAGS = \
	-DGREETER_DATA_DIR=\""$(greeterdatadir)"\" \
	-DCONFIG_FILE=\""$(configdir)/lightdm-gtk-greeter.conf"\" \
	-I$(top_srcdir)/liblightdm-gobject \
	$(LIGHTDM_GTK_GREETER_CFLAGS) \
	$(WARN_CFLAGS)

lightdm_gtk_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-1 \
	$(LIGHTDM_GTK_GREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
