lib_LTLIBRARIES = liblightdm-qt-0.la

# Generate metadata for QObjects
MOC_FILES = \
	ldmgreeter_moc.cpp \
	ldmsessionsmodel_moc.cpp

ldmgreeter_moc.cpp: ldmgreeter.h
	moc $< -o $@
ldmsessionsmodel_moc.cpp: ldmsessionsmodel.h
	moc $< -o $@

liblightdm_qt_0include_HEADERS = \
	ldmgreeter.h \
	ldmlanguage.h \
	ldmsessionsmodel.h \
	ldmuser.h \
	include/LdmGreeter \
	include/LdmLanguage \
	include/LdmSessionsModel \
	include/LdmUser
liblightdm_qt_0includedir=$(includedir)/lightdm-qt-0/lightdm

liblightdm_qt_0_la_LIBADD = $(LIBLIGHTDM_QT_LIBS)
liblightdm_qt_0_la_CXXFLAGS = $(LIBLIGHTDM_QT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(XSESSIONS_DIR)\"

liblightdm_qt_0_la_SOURCES = \
	ldmgreeter.cpp \
	ldmlanguage.cpp \
	ldmsessionsmodel.cpp \
	ldmuser.cpp \
	$(MOC_FILES)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-qt-0.pc

CLEANFILES = \
	$(MOC_FILES)

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
