bin_PROGRAMS = lightdm-qt-greeter

xgreetersdir = $(datadir)/xgreeters
dist_xgreeters_DATA = lightdm-qt-greeter.desktop

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

loginprompt.cpp: ui_loginprompt.h
panel.cpp: ui_panel.h

lightdm_qt_greeter_SOURCES = \
	greeter.cpp \
	greeter.h \
	loginprompt.cpp \
	loginprompt.h \
	main.cpp \
	panel.cpp \
	panel.h \
	greeter_moc.cpp \
	loginprompt_moc.cpp \
	panel_moc.cpp

lightdm_qt_greeter_CXXFLAGS = \
	-I$(top_srcdir)/liblightdm-qt \
	$(LIGHTDM_QT_GREETER_CFLAGS)

lightdm_qt_greeter_LDADD = \
	-L$(top_builddir)/liblightdm-qt \
	-llightdm-qt-1 \
	$(LIGHTDM_QT_GREETER_LIBS)

CLEANFILES = \
	greeter_moc.cpp \
	loginprompt_moc.cpp \
	panel_moc.cpp \
	ui_loginprompt.h \
	ui_panel.h

EXTRA_DIST = \
	loginprompt.ui \
	panel.ui

DISTCLEANFILES = \
	Makefile.in
