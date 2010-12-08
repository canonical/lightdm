SUBDIRS =
if COMPILE_LIBLIGHTDM_GOBJECT
SUBDIRS += liblightdm-gobject
endif
if COMPILE_LIBLIGHTDM_QT
SUBDIRS += liblightdm-qt
endif
SUBDIRS += data greeters po src themes doc

DISTCHECK_CONFIGURE_FLAGS = --enable-introspection --enable-gtk-doc

EXTRA_DIST = \
	autogen.sh \
	lightdm.doap

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
