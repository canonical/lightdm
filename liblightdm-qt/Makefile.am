common_libadd = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-1
liblightdm_qt_3_la_LIBADD = \
	$(LIBLIGHTDM_QT4_LIBS) \
	$(common_libadd)
liblightdm_qt5_3_la_LIBADD = \
	$(LIBLIGHTDM_QT5_LIBS) \
	$(common_libadd)

common_cflags = \
	-I$(top_srcdir)/liblightdm-gobject \
	$(LIBLIGHTDM_GOBJECT_CFLAGS) \
	-DQT_NO_KEYWORDS \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"
liblightdm_qt_3_la_CXXFLAGS = \
	$(LIBLIGHTDM_QT4_CFLAGS) \
	$(common_cflags)
liblightdm_qt5_3_la_CXXFLAGS = \
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

liblightdm_qt_3includedir=$(includedir)/lightdm-qt-3/QLightDM
liblightdm_qt5_3includedir=$(includedir)/lightdm-qt5-3/QLightDM

common_sources = \
	greeter.cpp \
	power.cpp \
	sessionsmodel.cpp \
	usersmodel.cpp
liblightdm_qt_3_la_SOURCES = \
	$(common_sources) \
	$(liblightdm_qt_3include_HEADERS)
liblightdm_qt5_3_la_SOURCES = \
	$(common_sources) \
	$(liblightdm_qt5_3include_HEADERS)

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
lib_LTLIBRARIES += liblightdm-qt-3.la
liblightdm_qt_3include_HEADERS = $(common_headers)
BUILT_SOURCES += $(common_sources:.cpp=_moc4.cpp)
pkgconfig_DATA += liblightdm-qt-3.pc
endif

if COMPILE_LIBLIGHTDM_QT5
lib_LTLIBRARIES += liblightdm-qt5-3.la
liblightdm_qt5_3include_HEADERS = $(common_headers)
BUILT_SOURCES += $(common_sources:.cpp=_moc5.cpp)
pkgconfig_DATA += liblightdm-qt5-3.pc
endif

# Support pretty printing MOC
AM_V_MOC4 = $(am__v_MOC4_$(V))
am__v_MOC4_ = $(am__v_MOC4_$(AM_DEFAULT_VERBOSITY))
am__v_MOC4_0 = @echo "  MOC4    " $@;
%_moc4.cpp: QLightDM/%.h
	$(AM_V_MOC4) $(MOC4) $< -o $@

AM_V_MOC5 = $(am__v_MOC5_$(V))
am__v_MOC5_ = $(am__v_MOC5_$(AM_DEFAULT_VERBOSITY))
am__v_MOC5_0 = @echo "  MOC5    " $@;
%_moc5.cpp: QLightDM/%.h
	$(AM_V_MOC5) $(MOC5) $< -o $@
