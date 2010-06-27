DOC_MODULE=ldmgreeter

DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

DOC_SOURCE_DIR=$(top_srcdir)/libldmgreeter

HFILE_GLOB=$(top_srcdir)/libldmgreeter/*.h
CFILE_GLOB=$(top_srcdir)/libldmgreeter/*.c

INCLUDES = \
	$(LIBLDMGREETER_CFLAGS) \
	-I$(top_srcdir)/libldmgreeter

GTKDOC_LIBS = \
	$(top_builddir)/libldmgreeter/libldmgreeter.la

MKDB_OPTIONS=--sgml-mode --output-format=xml --name-space=ldm

DISTCLEANFILES = $(DOC_MODULE)-docs.xml \
	$(DOC_MODULE)-overrides.txt

include $(top_srcdir)/gtk-doc.make
