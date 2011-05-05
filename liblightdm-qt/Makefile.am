lib_LTLIBRARIES = liblightdm-qt-0.la

# Generate metadata for QObjects
MOC_FILES = \
	consolekitinterface_moc.cpp \
	ldmgreeter_moc.cpp \
	powermanagementinterface_moc.cpp
consolekitinterface_moc.cpp: consolekitinterface.h
	moc $< -o $@
ldmgreeter_moc.cpp: ldmgreeter.h
	moc $< -o $@
powermanagementinterface_moc.cpp: powermanagementinterface.h
	moc $< -o $@

GENERATED_FILES = $(INTERFACE_FILES) $(MOC_FILES)    

liblightdm_qt_0include_HEADERS = \
	ldmgreeter.h \
	ldmlanguage.h \
	ldmsession.h \
	ldmuser.h \
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

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
