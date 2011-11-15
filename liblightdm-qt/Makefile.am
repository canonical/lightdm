lib_LTLIBRARIES = liblightdm-qt-1.la

power.cpp: QLightDM/power.h
	$(MOC) $< -o power_moc.cpp
greeter.cpp: QLightDM/greeter.h
	$(MOC) $< -o greeter_moc.cpp
session.cpp: QLightDM/session.h
	$(MOC) $< -o session_moc.cpp
user.cpp: QLightDM/user.h
	$(MOC) $< -o user_moc.cpp



liblightdm_qt_1_la_LIBADD = $(LIBLIGHTDM_QT_LIBS)
liblightdm_qt_1_la_CXXFLAGS = $(LIBLIGHTDM_QT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"

liblightdm_qt_1include_HEADERS = \
	QLightDM/Greeter \
	QLightDM/Power \
	QLightDM/Session \
	QLightDM/System \
	QLightDM/User \
	QLightDM/greeter.h \
	QLightDM/power.h \
	QLightDM/session.h \
	QLightDM/system.h \
	QLightDM/user.h

liblightdm_qt_1includedir=$(includedir)/lightdm-qt-1/QLightDM

liblightdm_qt_1_la_SOURCES = \
	greeter.cpp \
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
