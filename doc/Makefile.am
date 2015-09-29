DOC_MODULE=lightdm-gobject-1

DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

DOC_SOURCE_DIR=$(top_srcdir)/liblightdm-gobject

HFILE_GLOB=$(top_srcdir)/liblightdm-gobject/lightdm.h
CFILE_GLOB=$(top_srcdir)/liblightdm-gobject/*.c

AM_CPPFLAGS = \
	$(LIBLIGHTDM_GOBJECT_CFLAGS) \
	-I$(top_srcdir)/liblightdm-gobject

GTKDOC_LIBS = \
	$(LIBLIGHTDM_GOBJECT_LIBS) \
	$(top_builddir)/liblightdm-gobject/liblightdm-gobject-1.la

MKDB_OPTIONS=--sgml-mode --output-format=xml --name-space=LightDM

DISTCLEANFILES = \
	$(DOC_MODULE)-docs.xml \
	$(DOC_MODULE)-overrides.txt

include $(top_srcdir)/gtk-doc.make
