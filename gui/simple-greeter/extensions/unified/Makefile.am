NULL =
PAM_SERVICE_NAME = gdm

extensiondir = $(GDM_SIMPLE_GREETER_EXTENSIONS_DATA_DIR)/unified
extension_DATA = page.ui

AM_CPPFLAGS = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/gui/simple-greeter/libgdmsimplegreeter	\
	-DDMCONFDIR=\""$(dmconfdir)"\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"                  \
	-DPLUGINDATADIR=\""$(extensiondir)"\"		\
	-DGDM_UNIFIED_EXTENSION_SERVICE_NAME=\""$(PAM_SERVICE_NAME)"\"	\
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

noinst_LTLIBRARIES = libunified.la

libunified_la_CFLAGS =			\
	$(SIMPLE_GREETER_CFLAGS)	\
	$(NULL)

libunified_la_LDFLAGS = -export-dynamic
libunified_la_LIBADD = ../../../../common/libgdmcommon.la \
		../../libgdmsimplegreeter/libgdmsimplegreeter.la
libunified_la_SOURCES =				\
			gdm-unified-extension.h	\
			gdm-unified-extension.c

EXTRA_DIST = $(extension_DATA)

MAINTAINERCLEANFILES =                  \
        *~                              \
        Makefile.in
