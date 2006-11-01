## Process this file with automake to produce makefile.in

# Note that we have cflags for just about everything even if
# we don't link to everything
INCLUDES = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/vicious-extensions		\
	-DAUTHDIR=\"$(authdir)\"			\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DDMCONFDIR=\"$(dmconfdir)\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"			\
	-DGDMLOCALEDIR=\"$(gdmlocaledir)\"		\
	-DGDM_DEFAULTS_CONF=\"$(GDM_DEFAULTS_CONF)\"	\
	-DGDM_CUSTOM_CONF=\"$(GDM_CUSTOM_CONF)\"	\
	-DGDM_OLD_CONF=\"$(GDM_OLD_CONF)\"		\
	-DGDM_USER_PATH=\"$(GDM_USER_PATH)\"		\
	-DLIBDIR=\"$(libdir)\"				\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-DLOGDIR=\"$(logdir)\"				\
	-DPIXMAPDIR=\"$(pixmapdir)\"			\
	-DSBINDIR=\"$(sbindir)\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	$(GUI_CFLAGS)					\
	$(DAEMON_CFLAGS)				\
	-DPAM_PREFIX=\"$(PAM_PREFIX)\"			\
	$(GNOME_INCLUDEDIR)				\
	-DGREETERTHEMEDIR=\""$(datadir)/gdm/themes"\"

#
#	-DG_DISABLE_DEPRECATED				\
#	-DGDK_DISABLE_DEPRECATED			\
#	-DGDK_PIXBUF_DISABLE_DEPRECATED			\
#	-DGTK_DISABLE_DEPRECATED			\
#	-DGNOME_DISABLE_DEPRECATED			\
#

sbin_PROGRAMS = gdm-binary

gdm_binary_SOURCES = \
	gdm.c \
	gdm.h \
	gdmconfig.c \
	gdmconfig.h \
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
	getvt.h

if WITH_CONSOLE_KIT
gdm_binary_SOURCES += \
	gdmconsolekit.c \
	gdmconsolekit.h
endif

EXTRA_gdm_binary_SOURCES = verify-pam.c verify-crypt.c verify-shadow.c

# Note that these libs are in LDFLAGS because they should come before
# everything else on the link line as they may override stuff
gdm_binary_LDFLAGS = \
	$(EXTRA_DAEMON_LIBS)

gdm_binary_LDADD = \
	$(DAEMON_LIBS)				\
	$(INTLLIBS)				\
	$(GLIB_LIBS)				\
	$(GOBJECT_LIBS)				\
	$(GDK_LIBS)				\
	$(top_builddir)/vicious-extensions/libvicious.a \
	$(X_LIBS)				\
	$(XINERAMA_LIBS)			\
	$(XDMCP_LIBS)				\
	-lXau					\
	-lX11					\
	-lXext					\
	-lpopt

sbin_SCRIPTS = gdm
CLEANFILES = gdm

gdm: $(srcdir)/gdm.in
	sed -e 's,[@]sbindir[@],$(sbindir),g' <$(srcdir)/gdm.in >gdm

EXTRA_DIST = gdm.in
