if HAVE_QT
lib_LTLIBRARIES = libldmgreeter-qt.la
endif

GENERATED_FILES = \
	displayinterface.cpp \
	displayinterface.h \
	usermanagerinterface.cpp \
	usermanagerinterface.h

displayinterface.cpp displayinterface.h: $(top_srcdir)/src/display.xml
	qdbusxml2cpp $< -i ldmauthrequest.h -c DisplayInterface -p displayinterface
usermanagerinterface.cpp usermanagerinterface.h: $(top_srcdir)/src/user-manager.xml
	qdbusxml2cpp $< -i ldmuser.h -c UserManagerInterface -p usermanagerinterface

libldmgreeter_qtinclude_HEADERS = \
	consolekitinterface.h \
	displayinterface.h \
	ldmauthrequest.h \
	ldmgreeter.h \
	ldmlanguage.h \
	ldmsession.h \
	ldmuser.h \
	powermanagementinterface.h \
	usermanagerinterface.h
libldmgreeter_qtincludedir=$(includedir)/lightdm-qt-0/lightdm

libldmgreeter_qt_la_LIBADD = $(LIBLDMGREETER_QT_LIBS)
libldmgreeter_qt_la_CXXFLAGS = $(LIBLDMGREETER_QT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(XSESSIONS_DIR)\"

libldmgreeter_qt_la_SOURCES= \
	consolekitinterface.cpp \
	displayinterface.cpp \
	ldmauthrequest.cpp \
	ldmgreeter.cpp \
	ldmlanguage.cpp \
	ldmsession.cpp \
	ldmuser.cpp \
	powermanagementinterface.cpp \
	usermanagerinterface.cpp

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libldmgreeter-qt-0.pc

CLEANFILES = \
	$(GENERATED_FILES)

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
