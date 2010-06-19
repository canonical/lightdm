DOC_MODULE=ldmgreeter

DOC_SOURCE_DIR=$(top_srcdir)/libldmgreeter

DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

HFILE_GLOB=$(top_srcdir)/libldmgreeter/*.h
CFILE_GLOB=$(top_srcdir)/libldmgreeter/*.c

INCLUDES = \
	$(LIBLDMGREETER_CFLAGS) \
	-I$(top_srcdir)/libldmgreeter

GTKDOC_LIBS = \
	$(LIBLDMGREETER_LIBS) \
	-L$(top_builddir)/libldmgreeter \
	-lldmgreeter

include $(top_srcdir)/gtk-doc.make

DISTCLEANFILES = $(DOC_MODULE)-docs.xml $(DOC_MODULE)-overrides.txt $(DOC_MODULE)-sections.txt $(DOC_MODULE).types
