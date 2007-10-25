NULL =

INCLUDES = \
	-I$(top_srcdir)/common				\
	-DAUTHDIR=\""$(authdir)"\"		 	\
	-DDATADIR=\""$(datadir)"\"		 	\
	-DGDMCONFDIR=\"$(gdmconfdir)\"			\
	-DDMCONFDIR=\""$(dmconfdir)"\"		 	\
	-DGDMLOCALEDIR=\""$(gdmlocaledir)"\"		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	-DPIXMAPDIR=\""$(pixmapdir)"\"		 	\
	$(SETTINGS_DAEMON_CFLAGS)			\
	$(NULL)

libexec_PROGRAMS =			\
	gdm-settings-daemon		\
	$(NULL)

gdm_settings_daemon_SOURCES =  		\
	main.c				\
	gdm-settings-manager.c		\
	gdm-settings-manager.h		\
	$(NULL)

gdm_settings_daemon_LDADD = 		\
	$(SETTINGS_DAEMON_LIBS)		\
	$(top_builddir)/common/libgdmcommon.la	\
	$(NULL)

EXTRA_DIST = 			\
	$(NULL)
