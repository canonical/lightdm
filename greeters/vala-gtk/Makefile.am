libexec_PROGRAMS = lightdm-vala-gtk-greeter

lightdm_vala_gtk_greeter_SOURCES = \
	lightdm-vala-gtk-greeter.vala

lightdm_vala_gtk_greeter_VALAFLAGS = \
	--pkg gtk+-2.0 \
	--vapidir $(top_builddir)/liblightdm-gobject \    
	--pkg lightdm-0

lightdm_vala_gtk_greeter_CFLAGS = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(LIGHTDM_GTK_GREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_GTK_GREETER_BINARY=\"lightdm-vala-gtk-greeter\"

lightdm_vala_gtk_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-0 \
	$(LIGHTDM_GTK_GREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
