NULL =

SUBDIRS =		\
	plugins		\
	$(NULL)

INCLUDES = \
	-I$(top_srcdir)/common				\
	-DDATADIR=\""$(datadir)"\"		 	\
	-DGDM_SETTINGS_LOCALEDIR=\""$(datadir)/locale"\"\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	-DGDM_SETTINGS_PLUGINDIR=\""$(libdir)/gdm/settings/plugins"\"	\
	$(SETTINGS_DAEMON_CFLAGS)			\
	$(NULL)

libexec_PROGRAMS =			\
	gdm-settings-daemon		\
	$(NULL)

gdm_settings_daemon_SOURCES =  		\
	main.c				\
	gdm-settings-manager.c		\
	gdm-settings-manager.h		\
	gdm-settings-plugin.c		\
	gdm-settings-plugin.h		\
	gdm-settings-plugins-engine.c	\
	gdm-settings-plugins-engine.h	\
	gdm-settings-module.c		\
	gdm-settings-module.h		\
	$(NULL)

gdm_settings_daemon_LDADD = 		\
	$(SETTINGS_DAEMON_LIBS)		\
	$(top_builddir)/common/libgdmcommon.la	\
	$(NULL)

EXTRA_DIST = 			\
	$(NULL)
