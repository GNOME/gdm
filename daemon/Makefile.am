## Process this file with automake to produce makefile.in
CFLAGS = @CFLAGS@ -g -Wall \
	-Wpointer-arith \
	-Wmissing-prototypes -Wmissing-declarations 

DEFS = @DEFS@ -DGDM_CONFIG_FILE=\"@sysconfdir@/gdm/gdm.conf\"

INCLUDES = \
	-I.						\
	-I..						\
	-I$(includedir)					\
	-I$(top_srcdir)/vicious-extensions		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DLOCALEDIR=\"$(datadir)/locale\"		\
	-DPAM_PREFIX=\"$(PAM_PREFIX)\"			\
	$(GNOME_INCLUDEDIR)

bin_PROGRAMS = gdm

gdm_SOURCES = \
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
	gdm-net.h

EXTRA_gdm_SOURCES = verify-pam.c verify-crypt.c verify-shadow.c

gdm_LDADD = \
	$(GNOME_LIBDIR)				\
	$(GNOME_LIBS)				\
	$(GNOMEUI_LIBS)				\
	$(GTK_LIBS)				\
	$(XDMCP_LIBS)				\
	-lXau					\
	-lX11					\
	$(INTLLIBS)				\
	-L$(top_builddir)/vicious-extensions	\
	-lvicious

