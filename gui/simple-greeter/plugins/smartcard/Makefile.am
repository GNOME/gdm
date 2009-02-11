SUBDIRS = icons

NULL =
PAM_SERVICE_NAME = gdm-smartcard

extensiondir = $(extensionsdatadir)/smartcard
extension_DATA = page.ui

AM_CPPFLAGS = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/gui/simple-greeter/libnotificationarea	\
	-I$(top_srcdir)/gui/simple-greeter/libgdmsimplegreeter	\
	-DDMCONFDIR=\""$(dmconfdir)"\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"                  \
	-DPLUGINDATADIR=\""$(extensiondir)"\"		\
	-DPAMSERVICENAME=\""$(pam_DATA)"\"	\
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
plugin_LTLIBRARIES = smartcard.la

smartcard_la_CFLAGS =			\
	$(SIMPLE_GREETER_CFLAGS)	\
	$(NULL)

libexec_PROGRAMS = 			\
	gdm-smartcard-worker		\
	$(NULL)


smartcard_la_LDFLAGS = -module -avoid-version -export-dynamic
smartcard_la_LIBADD = ../../../../common/libgdmcommon.la \
			../../libgdmsimplegreeter/libgdmsimplegreeter.la
smartcard_la_SOURCES =				\
			gdm-smartcard-extension.h	\
			gdm-smartcard-extension.c	\
			plugin.c

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

$(PAM_SERVICE_NAME): $(PAM_SERVICE_NAME).pam
	cp $(PAM_SERVICE_NAME).pam $(PAM_SERVICE_NAME)

pamdir = $(PAM_PREFIX)/pam.d
pam_DATA = $(PAM_SERVICE_NAME)

EXTRA_DIST = $(extension_DATA) $(PAM_SERVICE_NAME).pam
CLEANFILES = $(PAM_SERVICE_NAME)

MAINTAINERCLEANFILES =                  \
        *~                              \
        $(PAM_SERVICE_NAME)             \
        Makefile.in
