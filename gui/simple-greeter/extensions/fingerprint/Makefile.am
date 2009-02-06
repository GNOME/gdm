SUBDIRS = icons

NULL =
PAM_SERVICE_NAME = gdm-fingerprint

gsettings_SCHEMAS =org.gnome.display-manager.extensions.fingerprint.gschema.xml
@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@

extensiondir = $(GDM_SIMPLE_GREETER_EXTENSIONS_DATA_DIR)/fingerprint
extension_DATA = page.ui

AM_CPPFLAGS = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/gui/simple-greeter/libgdmsimplegreeter	\
	-DDMCONFDIR=\""$(dmconfdir)"\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"                  \
	-DPLUGINDATADIR=\""$(extensiondir)"\"		\
	-DGDM_FINGERPRINT_EXTENSION_SERVICE_NAME=\""$(PAM_SERVICE_NAME)"\"	\
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
plugin_LTLIBRARIES = libfingerprint.la

libfingerprint_la_CFLAGS =			\
	$(SIMPLE_GREETER_CFLAGS)	\
	$(NULL)

libfingerprint_la_LDFLAGS = -module -avoid-version -export-dynamic
libfingerprint_la_LIBADD = ../../../../common/libgdmcommon.la \
			../../libgdmsimplegreeter/libgdmsimplegreeter.la
libfingerprint_la_SOURCES =				\
			gdm-fingerprint-extension.h	\
			gdm-fingerprint-extension.c

$(PAM_SERVICE_NAME): $(PAM_SERVICE_NAME).pam
	cp $(PAM_SERVICE_NAME).pam $(PAM_SERVICE_NAME)

pamdir = $(PAM_PREFIX)/pam.d
pam_DATA = $(PAM_SERVICE_NAME)

EXTRA_DIST = $(extension_DATA) $(gsettings_SCHEMAS) $(PAM_SERVICE_NAME).pam
CLEANFILES = $(PAM_SERVICE_NAME) $(gsettings_SCHEMAS)

MAINTAINERCLEANFILES =                  \
        *~                              \
        $(PAM_SERVICE_NAME)             \
        $(gsettings_SCHEMAS)            \
        Makefile.in
