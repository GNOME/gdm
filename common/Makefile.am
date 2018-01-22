## Process this file with automake to produce Makefile.in

NULL =

AM_CPPFLAGS = \
	-I.						\
	-I..						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
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
	-DGDM_RUN_DIR=\"$(GDM_RUN_DIR)\"	\
	-DGDM_RUNTIME_CONF=\"$(GDM_RUNTIME_CONF)\"	\
	-DGDM_SESSION_DEFAULT_PATH=\"$(GDM_SESSION_DEFAULT_PATH)\" \
	$(COMMON_CFLAGS)				\
	$(NULL)

noinst_LTLIBRARIES = 		\
	libgdmcommon.la		\
	$(NULL)

gdmdir = $(datadir)/gdm
gdm_DATA = gdb-cmd

if MKDTEMP_MISSING
MKDTEMP_FILES = mkdtemp.c mkdtemp.h
else
MKDTEMP_FILES =
endif

libgdmcommon_la_SOURCES =	\
	gdm-address.h		\
	gdm-address.c		\
	gdm-common.h		\
	gdm-common.c		\
	gdm-profile.c		\
	gdm-profile.h		\
	gdm-settings.c 		\
	gdm-settings.h		\
	gdm-settings-backend.c 	\
	gdm-settings-backend.h	\
	gdm-settings-desktop-backend.c 	\
	gdm-settings-desktop-backend.h	\
	gdm-settings-keys.h	\
	gdm-settings-utils.h	\
	gdm-settings-utils.c	\
	gdm-settings-direct.c 	\
	gdm-settings-direct.h	\
	gdm-log.h		\
	gdm-log.c		\
	$(MKDTEMP_FILES) 	\
	$(NULL)

libgdmcommon_la_CPPFLAGS = 		\
	$(AM_CPPFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(NULL)

libgdmcommon_la_CFLAGS =		\
	$(COMMON_CFLAGS)		\
	$(SYSTEMD_CFLAGS)	        \
	$(NULL)

libgdmcommon_la_LIBADD =		\
	$(SYSTEMD_LIBS)	        \
	$(NULL)

libgdmcommon_la_LDFLAGS = 	\
	-export-dynamic 	\
	$(NULL)

noinst_PROGRAMS = 		\
	test-log		\
	$(NULL)

test_log_SOURCES = 		\
	test-log.c	 	\
	$(NULL)

test_log_LDADD =		\
	libgdmcommon.la		\
	$(COMMON_LIBS)		\
	$(NULL)

EXTRA_DIST = 			\
	mkdtemp.c 		\
	mkdtemp.h		\
	$(gdm_DATA)		\
	$(NULL)
