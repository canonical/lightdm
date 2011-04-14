libexec_PROGRAMS = lightdm-qt-greeter

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
