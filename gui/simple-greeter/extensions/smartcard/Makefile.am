SUBDIRS = icons

NULL =
PAM_SERVICE_NAME = gdm-smartcard

extensiondir = $(GDM_SIMPLE_GREETER_EXTENSIONS_DATA_DIR)/smartcard
extension_DATA = page.ui

AM_CPPFLAGS = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/gui/simple-greeter/libgdmsimplegreeter	\
	-DDMCONFDIR=\""$(dmconfdir)"\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"                  \
	-DPLUGINDATADIR=\""$(extensiondir)"\"		\
	-DGDM_SMARTCARD_EXTENSION_SERVICE_NAME=\""$(PAM_SERVICE_NAME)"\"	\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DLIBLOCALEDIR=\""$(prefix)/lib/locale"\"	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\"			\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(GTK_CFLAGS)					\
	$(SIMPLE_GREETER_CFLAGS)			\
	$(POLKIT_GNOME_CFLAGS)				\
	$(NULL)

plugindir = $(GDM_SIMPLE_GREETER_PLUGINS_DIR)
plugin_LTLIBRARIES = libsmartcard.la

libsmartcard_la_CFLAGS =			\
	$(SIMPLE_GREETER_CFLAGS)	\
	$(NULL)

libexec_PROGRAMS = 			\
	gdm-smartcard-worker		\
	$(NULL)

libsmartcard_la_LDFLAGS = -module -avoid-version -export-dynamic
libsmartcard_la_LIBADD = ../../../../common/libgdmcommon.la \
			../../libgdmsimplegreeter/libgdmsimplegreeter.la
libsmartcard_la_SOURCES =				\
			gdm-smartcard-extension.h	\
			gdm-smartcard-extension.c

gdm_smartcard_worker_LDADD = ../../../../common/libgdmcommon.la \
				$(DAEMON_LIBS)		\
				$(GTHREAD_LIBS)		\
				$(NSS_LIBS)		\
				$(NULL)
gdm_smartcard_worker_CFLAGS =	$(DAEMON_CFLAGS)	\
				$(NSS_CFLAGS)		\
				$(NULL)
gdm_smartcard_worker_SOURCES =				\
				gdm-smartcard.h		\
				gdm-smartcard.c		\
				gdm-smartcard-manager.h	\
				gdm-smartcard-manager.c	\
				gdm-smartcard-worker.c	\
				$(NULL)

EXTRA_DIST = $(extension_DATA)

MAINTAINERCLEANFILES =                  \
        *~                              \
        Makefile.in
