libexec_PROGRAMS =

if HAVE_GTK_GREETER
libexec_PROGRAMS += lightdm-gtk-greeter
endif

if HAVE_QT_GREETER
libexec_PROGRAMS += lightdm-qt-greeter
endif

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

lightdm-qt-greeter_moc.cpp: lightdm-qt-greeter.h
	moc $< -o $@

lightdm_qt_greeter_SOURCES = \
	lightdm-qt-greeter.cpp \
	lightdm-qt-greeter_moc.cpp

lightdm_qt_greeter_CXXFLAGS = \
	-I$(top_srcdir)/liblightdm-qt \
	$(LIGHTDM_QT_GREETER_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_QT_GREETER_BINARY=\"lightdm-qt-greeter\"

lightdm_qt_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-qt \
	-llightdm-qt-0 \
	$(LIGHTDM_QT_GREETER_LIBS)

CLEANFILES = \
	lightdm-qt-greeter_moc.cpp

DISTCLEANFILES = \
	Makefile.in
