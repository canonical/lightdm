common_libadd = \
	-L$(top_builddir)/liblightdm-gobject \
	-llightdm-gobject-1
liblightdm_qt5_3_la_LIBADD = \
	$(LIBLIGHTDM_QT5_LIBS) \
	$(common_libadd)
liblightdm_qt6_3_la_LIBADD = \
    $(LIBLIGHTDM_QT6_LIBS) \
    $(common_libadd)

common_cflags = \
	$(WARN_CXXFLAGS) \
	-I$(top_srcdir)/liblightdm-gobject \
	$(GLIB_CFLAGS) \
	-DQT_NO_KEYWORDS
liblightdm_qt5_3_la_CXXFLAGS = \
	-fPIC \
	-DQT_DISABLE_DEPRECATED_BEFORE="QT_VERSION_CHECK(4, 0, 0)" \
	$(LIBLIGHTDM_QT5_CFLAGS) \
	$(common_cflags)
liblightdm_qt6_3_la_CXXFLAGS = \
	-fPIC \
	-DQT_DISABLE_DEPRECATED_BEFORE="QT_VERSION_CHECK(5, 0, 0)" \
	$(LIBLIGHTDM_QT6_CFLAGS) \
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

liblightdm_qt5_3includedir=$(includedir)/lightdm-qt5-3/QLightDM
liblightdm_qt6_3includedir=$(includedir)/lightdm-qt6-3/QLightDM

common_sources = \
	greeter.cpp \
	power.cpp \
	sessionsmodel.cpp \
	usersmodel.cpp
liblightdm_qt5_3_la_SOURCES = \
	$(common_sources) \
	$(liblightdm_qt5_3include_HEADERS)
liblightdm_qt6_3_la_SOURCES = \
	$(common_sources) \
	$(liblightdm_qt6_3include_HEADERS)

pkgconfigdir = $(libdir)/pkgconfig

CLEANFILES = \
	*_moc*.cpp

DISTCLEANFILES = \
	$(pkgconfig_DATA)

lib_LTLIBRARIES =
BUILT_SOURCES =
pkgconfig_DATA =

if COMPILE_LIBLIGHTDM_QT5
lib_LTLIBRARIES += liblightdm-qt5-3.la
liblightdm_qt5_3include_HEADERS = $(common_headers)
BUILT_SOURCES += $(common_sources:.cpp=_moc5.cpp)
pkgconfig_DATA += liblightdm-qt5-3.pc
endif

if COMPILE_LIBLIGHTDM_QT6
lib_LTLIBRARIES += liblightdm-qt6-3.la
liblightdm_qt6_3include_HEADERS = $(common_headers)
BUILT_SOURCES += $(common_sources:.cpp=_moc6.cpp)
pkgconfig_DATA += liblightdm-qt6-3.pc
endif

# Support pretty printing MOC
AM_V_MOC5 = $(am__v_MOC5_$(V))
am__v_MOC5_ = $(am__v_MOC5_$(AM_DEFAULT_VERBOSITY))
am__v_MOC5_0 = @echo "  MOC5    " $@;
%_moc5.cpp: QLightDM/%.h
	$(AM_V_MOC5) $(MOC5) $< -o $@

AM_V_MOC6 = $(am__v_MOC6_$(V))
am__v_MOC6_ = $(am__v_MOC6_$(AM_DEFAULT_VERBOSITY))
am__v_MOC6_0 = @echo "  MOC6    " $@;
%_moc6.cpp: QLightDM/%.h
	$(AM_V_MOC6) $(MOC6) $< -o $@
