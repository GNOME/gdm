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
	$(DBUS_CFLAGS)					\
	$(GLIB_CFLAGS)

noinst_LTLIBRARIES = 		\
	libgdmcommon.la		\
	$(null)

BUILT_SOURCES =			\
	gdm-settings-glue.h	\
	gdm-marshal.c		\
	gdm-marshal.h		\
	$(NULL)

gdm-settings-glue.h: gdm-settings.xml Makefile.am
	dbus-binding-tool --prefix=gdm_settings --mode=glib-server --output=gdm-settings-glue.h gdm-settings.xml

gdm-marshal.c: gdm-marshal.list
	echo "#include \"gdm-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=gdm_marshal --body >> $@

gdm-marshal.h: gdm-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=gdm_marshal --header > $@

libgdmcommon_la_SOURCES =	\
	gdm-address.h		\
	gdm-address.c		\
	gdm-marshal.c		\
	gdm-marshal.h		\
	gdm-common.h		\
	gdm-common.c		\
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
	gdm-settings-client.h	\
	gdm-settings-client.c	\
	gdm-log.h		\
	gdm-log.c		\
	gdm-md5.h		\
	gdm-md5.c		\
	gdm-signal-handler.h	\
	gdm-signal-handler.c	\
	ve-signal.h		\
	ve-signal.c		\
	$(NULL)

libgdmcommon_la_CPPFLAGS = 		\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(NULL)

libgdmcommon_la_CFLAGS =		\
	$(DBUS_CFLAGS)			\
	$(NULL)

libgdmcommon_la_LIBADD =		\
	$(NULL)

libgdmcommon_la_LDFLAGS = 	\
	-export-dynamic 	\
	$(NULL)

noinst_PROGRAMS = 		\
	test-settings-server	\
	test-settings-client	\
	test-log		\
	$(NULL)

test_log_SOURCES = 		\
	test-log.c	 	\
	$(NULL)

test_log_LDADD =		\
	libgdmcommon.a	\
	$(GLIB_LIBS)		\
	$(NULL)

test_settings_server_SOURCES = 		\
	test-settings-server.c	 	\
	$(NULL)

test_settings_server_LDADD =		\
	libgdmcommon.a			\
	$(GLIB_LIBS)			\
	$(DBUS_LIBS)			\
	$(NULL)

test_settings_client_SOURCES = 		\
	test-settings-client.c	 	\
	$(NULL)

test_settings_client_LDADD =		\
	libgdmcommon.a			\
	$(GLIB_LIBS)			\
	$(DBUS_LIBS)			\
	$(NULL)

CLEANFILES =			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	gdm-settings.xml	\
	gdm-marshal.list	\
	$(NULL)
