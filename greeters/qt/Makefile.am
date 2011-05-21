libexec_PROGRAMS = lightdm-example-qt-greeter

greeter_moc.cpp: greeter.h
	moc $< -o $@

loginprompt_moc.cpp: loginprompt.h
	moc $< -o $@

panel_moc.cpp: panel.h
	moc $< -o $@

ui_loginprompt.h: loginprompt.ui
	uic $< -o $@

ui_panel.h: panel.ui
	uic $< -o $@

lightdm_example_qt_greeter_SOURCES = \
	greeter.cpp \
	loginprompt.cpp \
	main.cpp \
	panel.cpp \
	greeter_moc.cpp \
	loginprompt_moc.cpp \
	panel_moc.cpp

lightdm_example_qt_greeter_CXXFLAGS = \
	-I$(top_srcdir)/liblightdm-qt \
        -I$(top_srcdir)/liblightdm-qt/include \
	$(LIGHTDM_EXAMPLE_QT_GREETER_CFLAGS)

lightdm_example_qt_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-qt \
	-llightdm-qt-0 \
	$(LIGHTDM_EXAMPLE_QT_GREETER_LIBS)

CLEANFILES = \
	greeter_moc.cpp \
	loginprompt_moc.cpp \
	panel_moc.cpp \
	ui_loginprompt.h \
	ui_panel.h


DISTCLEANFILES = \
	Makefile.in
