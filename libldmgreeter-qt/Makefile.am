if HAVE_QT
lib_LTLIBRARIES = libldmgreeter-qt.la
endif

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

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
