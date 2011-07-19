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

liblightdm_qt_1include_HEADERS = \
	QLightDM/greeter.h \
	QLightDM/language.h \
	QLightDM/power.h \
	QLightDM/sessionsmodel.h \
	QLightDM/system.h \
	QLightDM/user.h \
	QLightDM/usersmodel.h \
	QLightDM/Greeter \
	QLightDM/Language \
	QLightDM/Power \
	QLightDM/SessionsModel \
	QLightDM/System \
	QLightDM/User \
	QLightDM/UsersModel
liblightdm_qt_1includedir=$(includedir)/lightdm-qt-1/QLightDM

liblightdm_qt_1_la_SOURCES = \
	greeter.cpp \
	language.cpp \
	power.cpp \
	sessionsmodel.cpp \
	system.cpp \
	user.cpp \
	usersmodel.cpp \
	$(MOC_FILES) \
	$(liblightdm_qt_1include_HEADERS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-qt-1.pc

CLEANFILES = \
	$(MOC_FILES)

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
