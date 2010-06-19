lib_LTLIBRARIES = libldmgreeter.la

libldmgreeterinclude_HEADERS = \
	greeter.h

libldmgreeter_la_LIBADD = $(LIBLDMGREETER_LIBS)
libldmgreeter_la_CFLAGS = $(LIBLDMGREETER_CFLAGS)

libldmgreeter_la_SOURCES= \
	greeter.c

libldmgreeterincludedir=$(includedir)/lightdm-1.0

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libldmgreeter-1.pc

DISTCLEANFILES = \
	Makefile.in \
	libldmgreeter-1.pc
