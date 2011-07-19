DISTCLEANFILES = \
        Makefile.in

lib_LTLIBRARIES = liblightdm-qt-1.la

# Generate metadata for QObjects
MOC_FILES = \
	greeter_moc.cpp \
	sessionsmodel_moc.cpp \
	user_moc.cpp 

greeter_moc.cpp: QLightDM/Greeter
	moc $< -o $@
sessionsmodel_moc.cpp: QLightDM/SessionsModel
	moc $< -o $@
user_moc.cpp: QLightDM/User
	moc $< -o $@

liblightdm_qt_1_la_LIBADD = $(LIBLIGHTDM_QT_LIBS)
liblightdm_qt_1_la_CXXFLAGS = $(LIBLIGHTDM_QT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"

liblightdm_qt_1include_HEADERS = \
	QLightDM/Greeter \
	QLightDM/Language \
	QLightDM/Power \
	QLightDM/SessionsModel \
	QLightDM/System \
	QLightDM/User
liblightdm_qt_1includedir=$(includedir)/lightdm-qt-1/QLightDM

liblightdm_qt_1_la_SOURCES = \
	greeter.cpp \
	language.cpp \
	power.cpp \
	sessionsmodel.cpp \
	system.cpp \
	user.cpp \
	$(MOC_FILES) \
	$(liblightdm_qt_1include_HEADERS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-qt-1.pc

CLEANFILES = \
	$(MOC_FILES)

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
