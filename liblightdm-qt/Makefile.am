lib_LTLIBRARIES = liblightdm-qt-0.la

GENERATED_FILES = \
	displayinterface.cpp \
	displayinterface.h \
	usermanagerinterface.cpp \
	usermanagerinterface.h

displayinterface.cpp displayinterface.h: $(top_srcdir)/src/display.xml
	qdbusxml2cpp $< -i ldmauthrequest.h -c DisplayInterface -p displayinterface
usermanagerinterface.cpp usermanagerinterface.h: $(top_srcdir)/src/user-manager.xml
	qdbusxml2cpp $< -i ldmuser.h -c UserManagerInterface -p usermanagerinterface

ldmgreeter.cpp: displayinterface.h usermanagerinterface.h    

liblightdm_qt_0include_HEADERS = \
	consolekitinterface.h \
	displayinterface.h \
	ldmauthrequest.h \
	ldmgreeter.h \
	ldmlanguage.h \
	ldmsession.h \
	ldmuser.h \
	powermanagementinterface.h \
	usermanagerinterface.h
liblightdm_qt_0includedir=$(includedir)/lightdm-qt-0/lightdm

liblightdm_qt_0_la_LIBADD = $(LIBLIGHTDM_QT_LIBS)
liblightdm_qt_0_la_CXXFLAGS = $(LIBLIGHTDM_QT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(XSESSIONS_DIR)\"

liblightdm_qt_0_la_SOURCES= \
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
pkgconfig_DATA = liblightdm-qt-0.pc

CLEANFILES = \
	$(GENERATED_FILES)

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
