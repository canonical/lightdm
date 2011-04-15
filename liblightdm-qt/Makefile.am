lib_LTLIBRARIES = liblightdm-qt-0.la

# Generate interfaces from D-Bus definitions
INTERFACE_FILES = \
	usermanagerinterface.cpp \
	usermanagerinterface.h
usermanagerinterface.cpp usermanagerinterface.h: user-manager.xml
	qdbusxml2cpp $< -i ldmuser.h -c UserManagerInterface -p usermanagerinterface

# Generate metadata for QObjects
MOC_FILES = \
	consolekitinterface_moc.cpp \
	ldmgreeter_moc.cpp \
	powermanagementinterface_moc.cpp \
	usermanagerinterface_moc.cpp
consolekitinterface_moc.cpp: consolekitinterface.h
	moc $< -o $@
ldmgreeter_moc.cpp: ldmgreeter.h
	moc $< -o $@
powermanagementinterface_moc.cpp: powermanagementinterface.h
	moc $< -o $@
usermanagerinterface_moc.cpp: usermanagerinterface.h
	moc $< -o $@

GENERATED_FILES = $(INTERFACE_FILES) $(MOC_FILES)    

ldmgreeter.cpp: usermanagerinterface.h    

liblightdm_qt_0include_HEADERS = \
	ldmauthrequest.h \
	ldmgreeter.h \
	ldmlanguage.h \
	ldmsession.h \
	ldmuser.h \
	include/LdmAuthRequest \
	include/LdmGreeter \
	include/LdmLanguage \
	include/LdmSession \
	include/LdmUser
liblightdm_qt_0includedir=$(includedir)/lightdm-qt-0/lightdm

liblightdm_qt_0_la_LIBADD = $(LIBLIGHTDM_QT_LIBS)
liblightdm_qt_0_la_CXXFLAGS = $(LIBLIGHTDM_QT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(XSESSIONS_DIR)\"

liblightdm_qt_0_la_SOURCES = \
	consolekitinterface.cpp \
	consolekitinterface.h \
	ldmauthrequest.cpp \
	ldmgreeter.cpp \
	ldmlanguage.cpp \
	ldmsession.cpp \
	ldmuser.cpp \
	powermanagementinterface.cpp \
	powermanagementinterface.h \
	$(GENERATED_FILES)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-qt-0.pc

CLEANFILES = \
	$(GENERATED_FILES)

EXTRA_DIST = \
	user-manager.xml

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
