NULL =

modules_flags = -export_dynamic -avoid-version -module

plugindir = $(libdir)/gdm/settings/plugins

INCLUDES = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/gui/settings-daemon		\
	-DGDM_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	$(SETTINGS_PLUGIN_CFLAGS)			\
	$(NULL)

plugin_LTLIBRARIES = \
	libxsettings.la		\
	$(NULL)

libxsettings_la_SOURCES = 	\
	gdm-xsettings-plugin.h	\
	gdm-xsettings-plugin.c	\
	$(NULL)

libxsettings_la_LDFLAGS = 	\
	$(PLUGIN_LIBTOOL_FLAGS)	\
	$(modules_flags)	\
	$(NULL)

libxsettings_la_LIBADD  = 	\
	$(SETTINGS_PLUGIN_LIBS)	\
	$(NULL)

libxsettings_la_CFLAGS = 	\
	-I$(top_srcdir)/gui/settings-daemon		\
	$(NULL)

plugin_in_files = 		\
	xsettings.gdm-settings-plugin.desktop.in	\
	$(NULL)

%.gdm-settings-plugin: %.gdm-settings-plugin.desktop.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

plugin_DATA = $(plugin_in_files:.gdm-settings-plugin.desktop.in=.gdm-settings-plugin)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(plugin_DATA)		\
	$(NULL)

DISTCLEANFILES =		\
	$(plugin_DATA)		\
	$(NULL)

