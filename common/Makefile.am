## Process this file with automake to produce Makefile.in

NULL =

INCLUDES = \
	-I.					\
	-I..					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-DAUTHDIR=\"$(authdir)\"			\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DDMCONFDIR=\"$(dmconfdir)\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"			\
	-DGDMLOCALEDIR=\"$(gdmlocaledir)\"		\
	-DLIBDIR=\"$(libdir)\"				\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-DLOGDIR=\"$(logdir)\"				\
	-DPIXMAPDIR=\"$(pixmapdir)\"			\
	-DSBINDIR=\"$(sbindir)\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DGDM_DEFAULTS_CONF=\"$(GDM_DEFAULTS_CONF)\"	\
	-DGDM_CUSTOM_CONF=\"$(GDM_CUSTOM_CONF)\"	\
	-DGDM_OLD_CONF=\"$(GDM_OLD_CONF)\"		\
	$(GLIB_CFLAGS)

noinst_LIBRARIES = 		\
	libgdmcommon.a		\
	$(null)

libgdmcommon_a_SOURCES =	\
	gdm-address.h		\
	gdm-address.c		\
	gdm-common.h		\
	gdm-common.c		\
	gdm-common-config.h	\
	gdm-common-config.c	\
	gdm-config.h		\
	gdm-config.c		\
	gdm-log.h		\
	gdm-log.c		\
	gdm-md5.h		\
	gdm-md5.c		\
	gdm-signal-handler.h	\
	gdm-signal-handler.c	\
	ve-signal.h		\
	ve-signal.c		\
	$(NULL)

noinst_PROGRAMS = 		\
	test-config		\
	test-log		\
	$(NULL)

test_config_SOURCES = 		\
	$(top_builddir)/daemon/gdm-daemon-config-entries.h	\
	test-config.c	 	\
	$(NULL)

test_config_LDADD =		\
	libgdmcommon.a	\
	$(GLIB_LIBS)		\
	$(NULL)

test_log_SOURCES = 		\
	test-log.c	 	\
	$(NULL)

test_log_LDADD =		\
	libgdmcommon.a	\
	$(GLIB_LIBS)		\
	$(NULL)
