SUBDIRS =
if COMPILE_LIBLIGHTDM_GOBJECT
SUBDIRS += liblightdm-gobject
endif
SUBDIRS += liblightdm-qt
if COMPILE_TESTS
SUBDIRS += tests
endif
SUBDIRS += data po src doc help utils

DISTCHECK_CONFIGURE_FLAGS = --enable-introspection --enable-gtk-doc

EXTRA_DIST = \
	autogen.sh \
	lightdm.doap \
	COPYING.GPL3 \
	COPYING.LGPL2 \
	COPYING.LGPL3 \
	NEWS

DISTCLEANFILES = \
	Makefile.in \
	aclocal.m4 \
	configure \
	config.h.in \
	config.h \
	depcomp \
	gtk-doc.make \
	install-sh \
	missing \
	mkinstalldirs \
	omf.make \
	xmldocs.make \
	po/Makefile.in.in
