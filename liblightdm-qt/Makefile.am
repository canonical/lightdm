SUBDIRS = QLightDM

DISTCLEANFILES = \
        Makefile.in

lib_LTLIBRARIES = liblightdm-qt-1.la

# Generate metadata for QObjects
MOC_FILES = \
	greeter_moc.cpp \
	sessionsmodel_moc.cpp \
	usersmodel_moc.cpp 

greeter_moc.cpp: QLightDM/greeter.h
	moc $< -o $@
sessionsmodel_moc.cpp: QLightDM/sessionsmodel.h
	moc $< -o $@
usersmodel_moc.cpp: QLightDM/usersmodel.h
	moc $< -o $@

liblightdm_qt_1_la_LIBADD = $(LIBLIGHTDM_QT_LIBS)
liblightdm_qt_1_la_CXXFLAGS = $(LIBLIGHTDM_QT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"

liblightdm_qt_1_la_SOURCES = \
	greeter.cpp \
	language.cpp \
	power.cpp \
	sessionsmodel.cpp \
	system.cpp \
	user.cpp \
	usersmodel.cpp \
	$(MOC_FILES)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-qt-1.pc

CLEANFILES = \
	$(MOC_FILES)

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
