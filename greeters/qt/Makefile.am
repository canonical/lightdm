libexec_PROGRAMS = lightdm-example-qt-greeter

lightdm-example-qt-greeter_moc.cpp: lightdm-example-qt-greeter.h
	moc $< -o $@

lightdm_example_qt_greeter_SOURCES = \
	lightdm-example-qt-greeter.cpp \
	lightdm-example-qt-greeter.h \
	lightdm-example-qt-greeter_moc.cpp

lightdm_example_qt_greeter_CXXFLAGS = \
	-I$(top_srcdir)/liblightdm-qt \
	$(LIGHTDM_EXAMPLE_QT_GREETER_CFLAGS)

lightdm_example_qt_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-qt \
	-llightdm-qt-0 \
	$(LIGHTDM_EXAMPLE_QT_GREETER_LIBS)

CLEANFILES = \
	lightdm-example-qt-greeter_moc.cpp

DISTCLEANFILES = \
	Makefile.in
