NULL =
PAM_SERVICE_NAME = gdm-password

extensiondir = $(extensionsdatadir)/password
extension_DATA = page.ui

AM_CPPFLAGS = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/gui/simple-greeter/libnotificationarea	\
	-I$(top_srcdir)/gui/simple-greeter/libgdmsimplegreeter	\
	-DDMCONFDIR=\""$(dmconfdir)"\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"                  \
	-DPLUGINDATADIR=\""$(extensiondir)"\"		\
	-DPAMSERVICENAME=\""$(PAM_SERVICE_NAME)"\"	\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DLIBLOCALEDIR=\""$(prefix)/lib/locale"\"	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(GTK_CFLAGS)					\
	$(SIMPLE_GREETER_CFLAGS)			\
	$(POLKIT_GNOME_CFLAGS)				\
	$(NULL)

plugindir = $(GDM_SIMPLE_GREETER_PLUGINS_DIR)
plugin_LTLIBRARIES = password.la

password_la_CFLAGS =			\
	$(SIMPLE_GREETER_CFLAGS)	\
	$(NULL)

password_la_LDFLAGS = -module -avoid-version -export-dynamic
password_la_LIBADD = ../../../../common/libgdmcommon.la \
			../../libgdmsimplegreeter/libgdmsimplegreeter.la
password_la_SOURCES =				\
			gdm-password-extension.h	\
			gdm-password-extension.c	\
			plugin.c

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
