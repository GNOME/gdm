NULL =
PAM_SERVICE_NAME = gdm-password

extensiondir = $(GDM_SIMPLE_GREETER_EXTENSIONS_DATA_DIR)/password
extension_DATA = page.ui

AM_CPPFLAGS = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/gui/simple-greeter/libgdmsimplegreeter	\
	-DDMCONFDIR=\""$(dmconfdir)"\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"                  \
	-DPLUGINDATADIR=\""$(extensiondir)"\"		\
	-DGDM_PASSWORD_EXTENSION_SERVICE_NAME=\""$(PAM_SERVICE_NAME)"\"	\
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
plugin_LTLIBRARIES = libpassword.la

libpassword_la_CFLAGS =			\
	$(SIMPLE_GREETER_CFLAGS)	\
	$(NULL)

libpassword_la_LDFLAGS = -module -avoid-version -export-dynamic
libpassword_la_LIBADD = ../../../../common/libgdmcommon.la \
			../../libgdmsimplegreeter/libgdmsimplegreeter.la
libpassword_la_SOURCES =				\
			gdm-password-extension.h	\
			gdm-password-extension.c

EXTRA_DIST = $(extension_DATA)

MAINTAINERCLEANFILES =                  \
        *~                              \
        Makefile.in
