## Process this file with automake to produce makefile.in
CFLAGS = @CFLAGS@ -g -Wall \
	-Wpointer-arith \
	-Wmissing-prototypes -Wmissing-declarations 

DEFS = @DEFS@ -DGDM_CONFIG_FILE=\"@sysconfdir@/gdm/gdm.conf\"

INCLUDES = \
	-I.						\
	-I..						\
	-DG_DISABLE_DEPRECATED				\
	-DGDK_DISABLE_DEPRECATED			\
	-DGDK_PIXBUF_DISABLE_DEPRECATED			\
	-DGTK_DISABLE_DEPRECATED			\
	-DGNOME_DISABLE_DEPRECATED			\
	-I$(top_srcdir)/vicious-extensions		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DLOCALEDIR=\"$(datadir)/locale\"		\
	$(DAEMON_CFLAGS)				\
	-DPAM_PREFIX=\"$(PAM_PREFIX)\"			\
	$(GNOME_INCLUDEDIR)


bin_PROGRAMS = gdm-binary

gdm_binary_SOURCES = \
	gdm.c \
	gdm.h \
	display.c \
	display.h \
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

EXTRA_gdm_binary_SOURCES = verify-pam.c verify-crypt.c verify-shadow.c

# Note that these libs are in LDFLAGS because they should come before
# everything else on the link line as they may override stuff
gdm_binary_LDFLAGS = \
	$(EXTRA_DAEMON_LIBS)

gdm_binary_LDADD = \
	$(DAEMON_LIBS)				\
	$(X_LIBS)				\
	$(XDMCP_LIBS)				\
	-lXau					\
	-lX11					\
	-lXext					\
	$(XINERAMA_LIBS)			\
	$(INTLLIBS)				\
	-L$(top_builddir)/vicious-extensions	\
	-lvicious

bin_SCRIPTS = gdm

EXTRA_DIST = gdm.in
