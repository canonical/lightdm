bin_PROGRAMS = dm-tool
pkglibexec_PROGRAMS = lightdm-set-defaults
dist_pkglibexec_SCRIPTS = gdmflexiserver

dm_tool_SOURCES = \
	dm-tool.c

dm_tool_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\" \
	-DLOCALE_DIR=\"$(datadir)/locale\"

dm_tool_LDADD = \
	$(LIGHTDM_LIBS)

lightdm_set_defaults_SOURCES = \
	lightdm-set-defaults.c

lightdm_set_defaults_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\" \
	-DLOCALE_DIR=\"$(datadir)/locale\"

lightdm_set_defaults_LDADD = \
	$(LIGHTDM_LIBS)

DISTCLEANFILES = \
    Makefile.in

