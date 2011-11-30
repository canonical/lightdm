lib_LTLIBRARIES = liblightdm-qt-2.la

%_moc.cpp: QLightDM/%.h
	$(MOC) $< -o $@

power.cpp: power_moc.cpp
greeter.cpp: greeter_moc.cpp
sessionsmodel.cpp: sessionsmodel_moc.cpp
usersmodel.cpp: usersmodel_moc.cpp

liblightdm_qt_2_la_LIBADD = \
	$(LIBLIGHTDM_QT_LIBS) \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-1

liblightdm_qt_2_la_CXXFLAGS = $(LIBLIGHTDM_QT_CFLAGS) \
	-I$(top_srcdir)/liblightdm-gobject \
	$(LIBLIGHTDM_GOBJECT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"

liblightdm_qt_2include_HEADERS = \
	QLightDM/Greeter \
	QLightDM/Power \
	QLightDM/SessionsModel \
	QLightDM/System \
	QLightDM/UsersModel \
	QLightDM/greeter.h \
	QLightDM/power.h \
	QLightDM/sessionsmodel.h \
	QLightDM/system.h \
	QLightDM/usersmodel.h

liblightdm_qt_2includedir=$(includedir)/lightdm-qt-2/QLightDM

liblightdm_qt_2_la_SOURCES = \
	greeter.cpp \
	power.cpp \
	sessionsmodel.cpp \
	system.cpp \
	usersmodel.cpp \
	$(liblightdm_qt_2include_HEADERS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-qt-2.pc

CLEANFILES = \
	*_moc.cpp

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
