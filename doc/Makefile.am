DOC_MODULE=ldmgreeter

DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

DOC_SOURCE_DIR=$(top_srcdir)/libldmgreeter-gobject

HFILE_GLOB=$(top_srcdir)/libldmgreeter-gobject/*.h
CFILE_GLOB=$(top_srcdir)/libldmgreeter-gobject/*.c

INCLUDES = \
	$(LIBLDMGREETER_GOBJECT_CFLAGS) \
	-I$(top_srcdir)/libldmgreeter-gobject

GTKDOC_LIBS = \
	$(LIBLDMGREETER_GOBJECT_LIBS) \
	$(top_builddir)/libldmgreeter-gobject/libldmgreeter-gobject.la

MKDB_OPTIONS=--sgml-mode --output-format=xml --name-space=ldm

DISTCLEANFILES = \
	Makefile.in \
	$(DOC_MODULE)-docs.xml \
	$(DOC_MODULE)-overrides.txt

include $(top_srcdir)/gtk-doc.make
