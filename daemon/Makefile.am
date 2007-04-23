NULL =

# Note that we have cflags for just about everything even if
# we don't link to everything
INCLUDES = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/common				\
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
	$(GUI_CFLAGS)					\
	$(DAEMON_CFLAGS)				\
	-DLANG_CONFIG_FILE=\"$(LANG_CONFIG_FILE)\"	\
	$(GNOME_INCLUDEDIR)				\
	-DGREETERTHEMEDIR=\""$(datadir)/gdm/themes"\"	\
	$(NULL)

sbin_PROGRAMS = gdm-binary		\
	$(NULL)

gdm_binary_SOURCES = \
	gdm.c				\
	gdm.h \
	gdm-daemon-config.c \
	gdm-daemon-config.h \
	gdm-daemon-config-entries.h \
	gdm-daemon-config-keys.h \
	display.c \
	display.h \
	fstype.c \
	slave.c \
	slave.h \
	server.c \
	server.h \
	misc.c \
	misc.h \
	auth.c \
	auth.h \
	cookie.c \
	cookie.h \
	xdmcp.c \
	xdmcp.h \
	choose.c \
	choose.h \
	filecheck.c \
	filecheck.h \
	md5.c \
	md5.h \
	@VRFY@.c \
	verify.h \
	errorgui.c \
	errorgui.h \
	gdm-net.c \
	gdm-net.h \
	getvt.c \
	getvt.h	\
	$(NULL)

XDMCP_SOURCES =			\
	gdm-xdmcp-manager.c	\
	gdm-xdmcp-manager.h	\
	$(NULL)

if XDMCP_SUPPORT
gdm_binary_SOURCES += $(XDMCP_SOURCES)
endif

EXTRA_gdm_binary_SOURCES = 	\
	verify-pam.c		\
	verify-crypt.c		\
	verify-shadow.c		\
	$(XDMCP_SOURCES)	\
	$(NULL)

CONSOLE_KIT_SOURCES = 		\
	gdmconsolekit.c		\
	gdmconsolekit.h		\
	$(NULL)

EXTRA_gdm_binary_SOURCES += $(CONSOLE_KIT_SOURCES)

# Note that these libs are in LDFLAGS because they should come before
# everything else on the link line as they may override stuff
gdm_binary_LDFLAGS =				\
	$(EXTRA_DAEMON_LIBS)			\
	$(NULL)

gdm_binary_LDADD = \
	$(DAEMON_LIBS)				\
	$(INTLLIBS)				\
	$(GLIB_LIBS)				\
	$(GOBJECT_LIBS)				\
	$(GDK_LIBS)				\
	$(top_builddir)/common/libgdmcommon.a	\
	$(X_LIBS)				\
	$(XINERAMA_LIBS)			\
	$(XDMCP_LIBS)				\
	-lXau					\
	-lX11					\
	-lXext					\
	$(NULL)

if WITH_CONSOLE_KIT
gdm_binary_SOURCES += $(CONSOLE_KIT_SOURCES)
gdm_binary_LDADD += $(DBUS_LIBS)
INCLUDES += $(DBUS_CFLAGS)
endif

sbin_SCRIPTS = gdm
CLEANFILES = gdm

gdm: $(srcdir)/gdm.in
	sed -e 's,[@]sbindir[@],$(sbindir),g' <$(srcdir)/gdm.in >gdm

EXTRA_DIST = gdm.in
