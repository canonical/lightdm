%_moc4.cpp: QLightDM/%.h
	$(MOC4) $< -o $@

%_moc5.cpp: QLightDM/%.h
	$(MOC5) $< -o $@

common_libadd = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-1
liblightdm_qt_2_la_LIBADD = \
	$(LIBLIGHTDM_QT4_LIBS) \
	$(common_libadd)
liblightdm_qt5_2_la_LIBADD = \
	$(LIBLIGHTDM_QT5_LIBS) \
	$(common_libadd)

common_cflags = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(LIBLIGHTDM_GOBJECT_CFLAGS) \
	-DQT_NO_KEYWORDS \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"
liblightdm_qt_2_la_CXXFLAGS = \
	$(LIBLIGHTDM_QT4_CFLAGS) \
	$(common_cflags)
liblightdm_qt5_2_la_CXXFLAGS = \
	-fPIC \
	-DQT_DISABLE_DEPRECATED_BEFORE="QT_VERSION_CHECK(4, 0, 0)" \
	$(LIBLIGHTDM_QT5_CFLAGS) \
	$(common_cflags)

common_headers = \
	QLightDM/Greeter \
	QLightDM/Power \
	QLightDM/SessionsModel \
	QLightDM/UsersModel \
	QLightDM/greeter.h \
	QLightDM/power.h \
	QLightDM/sessionsmodel.h \
	QLightDM/usersmodel.h

liblightdm_qt_2includedir=$(includedir)/lightdm-qt-2/QLightDM
liblightdm_qt5_2includedir=$(includedir)/lightdm-qt5-2/QLightDM

common_sources = \
	greeter.cpp \
	power.cpp \
	sessionsmodel.cpp \
	usersmodel.cpp
liblightdm_qt_2_la_SOURCES = \
	$(common_sources) \
	$(liblightdm_qt_2include_HEADERS)
liblightdm_qt5_2_la_SOURCES = \
	$(common_sources) \
	$(liblightdm_qt5_2include_HEADERS)

pkgconfigdir = $(libdir)/pkgconfig

CLEANFILES = \
	*_moc*.cpp

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)

lib_LTLIBRARIES =
BUILT_SOURCES =
pkgconfig_DATA =

if COMPILE_LIBLIGHTDM_QT4
lib_LTLIBRARIES += liblightdm-qt-2.la
liblightdm_qt_2include_HEADERS = $(common_headers)
BUILT_SOURCES += $(common_sources:.cpp=_moc4.cpp)
pkgconfig_DATA += liblightdm-qt-2.pc
endif

if COMPILE_LIBLIGHTDM_QT5
lib_LTLIBRARIES += liblightdm-qt5-2.la
liblightdm_qt5_2include_HEADERS = $(common_headers)
BUILT_SOURCES += $(common_sources:.cpp=_moc5.cpp)
pkgconfig_DATA += liblightdm-qt5-2.pc
endif
