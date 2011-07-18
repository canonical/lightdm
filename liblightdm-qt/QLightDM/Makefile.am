lib_LTLIBRARIES = liblightdm-qt-0.la

# Generate metadata for QObjects
MOC_FILES = \
	greeter_moc.cpp \
	sessionsmodel_moc.cpp \
	usersmodel_moc.cpp 

greeter_moc.cpp: greeter.h
	moc $< -o $@
sessionsmodel_moc.cpp: sessionsmodel.h
	moc $< -o $@
usersmodel_moc.cpp: usersmodel.h
	moc $< -o $@


liblightdm_qt_0include_HEADERS = \
	greeter.h \
	language.h \
	sessionsmodel.h \
	user.h \
	usersmodel.h \
	UsersModel \
	Greeter \
	Language \
	SessionsModel \
	User

liblightdm_qt_0includedir=$(includedir)/lightdm-qt-0/QLightDM

liblightdm_qt_0_la_LIBADD = $(LIBLIGHTDM_QT_LIBS)
liblightdm_qt_0_la_CXXFLAGS = $(LIBLIGHTDM_QT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"

liblightdm_qt_0_la_SOURCES = \
	greeter.cpp \
	language.cpp \
	sessionsmodel.cpp \
	user.cpp \
	usersmodel.cpp \
	$(MOC_FILES)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-qt-0.pc

CLEANFILES = \
	$(MOC_FILES)

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA)
