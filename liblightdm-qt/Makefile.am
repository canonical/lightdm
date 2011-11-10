lib_LTLIBRARIES = liblightdm-qt-1.la

greeter_moc.cpp: QLightDM/greeter.h
	$(MOC) $< -o $@
session_moc.cpp: QLightDM/session.h
	$(MOC) $< -o $@
user_moc.cpp: QLightDM/user.h
	$(MOC) $< -o $@

liblightdm_qt_1_la_LIBADD = $(LIBLIGHTDM_QT_LIBS)
liblightdm_qt_1_la_CXXFLAGS = $(LIBLIGHTDM_QT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"

liblightdm_qt_1include_HEADERS = \
	QLightDM/Greeter \
	QLightDM/Language \
	QLightDM/Power \
	QLightDM/Session \
	QLightDM/System \
	QLightDM/User \
	QLightDM/greeter.h \
	QLightDM/language.h \
	QLightDM/power.h \
	QLightDM/session.h \
	QLightDM/system.h \
	QLightDM/user.h

liblightdm_qt_1includedir=$(includedir)/lightdm-qt-1/QLightDM

liblightdm_qt_1_la_SOURCES = \
	greeter.cpp \
	language.cpp \
	power.cpp \
	session.cpp \
	system.cpp \
	user.cpp \
	$(liblightdm_qt_1include_HEADERS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-qt-1.pc

CLEANFILES = \
	*_moc.cpp

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
