NULL =

SUBDIRS = . greeter modules faces

## Process this file with automake to produce makefile.in
DEFS = @DEFS@ \
	-DPREFIX=\"@prefix@\" 				\
	-DAUTHDIR=\"@authdir@\" 			\
	-DBINDIR=\"@bindir@\" 				\
	-DDATADIR=\"@datadir@\" 			\
	-DDMCONFDIR=\"@dmconfdir@\" 			\
	-DGDMCONFDIR=\"@gdmconfdir@\"			\
	-DGDMLOCALEDIR=\"@gdmlocaledir@\" 		\
	-DGDM_GLADE_DIR=\"@datadir@/gdm\" 		\
	-DLIBDIR=\"@libdir@\" 				\
	-DLIBEXECDIR=\"@libexecdir@\" 			\
	-DLOGDIR=\"@logdir@\"				\
	-DPIXMAPDIR=\"@pixmapdir@\" 			\
	-DSBINDIR=\"@sbindir@\"				\
	$(NULL)

INCLUDES = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/daemon				\
	-I$(top_srcdir)/common				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(GUI_CFLAGS)

#
#	-DG_DISABLE_DEPRECATED				\
#	-DGDK_DISABLE_DEPRECATED			\
#	-DGDK_PIXBUF_DISABLE_DEPRECATED			\
#	-DGTK_DISABLE_DEPRECATED			\
#	-DGNOME_DISABLE_DEPRECATED			\
#

noinst_LIBRARIES = libgdmwm.a libgdmcommon.a

libexec_PROGRAMS = \
	@GDMCHOOSER@ 		\
	gdmlogin

sbin_PROGRAMS = \
	gdmsetup

bin_PROGRAMS = \
	gdmphotosetup 		\
	@GDMXNESTCHOOSER@	\
	gdmflexiserver      \
	gdmdynamic
EXTRA_PROGRAMS = gdmchooser gdmXnestchooser

gdmphotosetup_SOURCES = \
	gdmphotosetup.c	

gdmXnestchooser_SOURCES = \
	gdmXnestchooser.c

gdmflexiserver_SOURCES = \
	gdmflexiserver.c

gdmdynamic_SOURCES = \
	gdmdynamic.c

libgdmwm_a_SOURCES = \
	gdmwm.c			\
	gdmwm.h

libgdmcommon_a_SOURCES = \
	gdmlanguages.c		\
	gdmlanguages.h		\
	gdmuser.c		\
	gdmuser.h		\
	gdmsession.c		\
	gdmsession.h		\
	misc.c			\
	misc.h			\
	gdmcomm.c		\
	gdmcomm.h		\
	gdmconfig.c		\
	gdmconfig.h		\
	gdmcommon.c		\
	gdmcommon.h

gdmchooser_SOURCES = \
	gdmchooser.c		\
	misc.c			\
	misc.h			

gdmlogin_SOURCES = \
	gdmlogin.c

gdmsetup_SOURCES = \
	gdmsetup.c

gdmchooser_LDFLAGS = -export-dynamic
gdmchooser_LDADD = \
	$(EXTRA_CHOOSER_LIBS)	\
	libgdmwm.a		\
	libgdmcommon.a		\
	$(GUIGLADE_LIBS)	\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(PANGO_LIBS)		\
	$(GDK_LIBS)		\
	$(GDKPIXBUF_LIBS)	\
	$(top_builddir)/common/libgdmcommon.a \
	$(X_EXTRA_LIBS)		\
	$(XINERAMA_LIBS)	\
	$(X_LIBS)		\
	$(XDMCP_LIBS)		\
	-lX11			\
	-lpopt

gdmlogin_LDADD = \
	libgdmwm.a		\
	libgdmcommon.a		\
	$(GUING_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(PANGO_LIBS)		\
	$(GDKPIXBUF_LIBS)	\
	$(top_builddir)/common/libgdmcommon.a \
	$(X_EXTRA_LIBS)		\
	$(XINERAMA_LIBS)	\
	$(X_LIBS)		\
	-lX11

# LIBGNOMEUI should be removed from gdmsetup at some point.
# It is needed because it uses gnome_help_display_uri(), which
# should be replaced at some point.
#
gdmsetup_CFLAGS = $(LIBGNOMEUI_CFLAGS)
gdmsetup_LDFLAGS = -export-dynamic
gdmsetup_LDADD = \
	libgdmcommon.a		\
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(PANGO_LIBS)		\
	$(GDK_LIBS)		\
	$(GDKPIXBUF_LIBS)	\
	$(EXTRA_SETUP_LIBS)	\
	$(LIBGNOMEUI_LIBS)	\
	$(top_builddir)/common/libgdmcommon.a \
	$(X_LIBS)		\
	-lXau			\
	-lX11

gdmphotosetup_LDADD = \
	libgdmcommon.a		\
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(GDKPIXBUF_LIBS)	\
	$(INTLLIBS)		\
	-lX11			\
	$(top_builddir)/common/libgdmcommon.a	\
	$(NULL)

gdmXnestchooser_LDADD = \
	libgdmcommon.a		\
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(top_builddir)/common/libgdmcommon.a \
	$(EXTRA_XNEST_LIBS)	\
	$(X_LIBS)		\
	-lXau			\
	-lX11			\
	-lpopt

gdmflexiserver_LDADD = \
	libgdmcommon.a		\
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(top_builddir)/common/libgdmcommon.a \
	$(EXTRA_FLEXI_LIBS)	\
	$(X_LIBS)		\
	-lX11			\
	-lXau			\
	-lpopt

gdmdynamic_LDADD = 		\
	libgdmcommon.a		\
	$(GUI_LIBS)		\
	$(INTLLIBS) 		\
	$(GLIB_LIBS) 		\
	$(GOBJECT_LIBS) 	\
	$(GDK_LIBS) 		\
	$(top_builddir)/common/libgdmcommon.a \
	$(EXTRA_DYNAMIC_LIBS)	\
	$(X_LIBS)		\
	-lX11			\
	-lXau

Settingsdir = $(datadir)/applications
Settings_files = gdmphotosetup.desktop
Settings_in_files = gdmphotosetup.desktop.in
Settings_DATA = $(Settings_in_files:.desktop.in=.desktop)

Systemdir = $(datadir)/applications
System_files = \
	gdmsetup.desktop	\
	gdmflexiserver.desktop	\
	gdmflexiserver-xnest.desktop
System_dist_files = \
	gdmsetup.desktop.in.in		\
	gdmflexiserver.desktop.in	\
	gdmflexiserver-xnest.desktop.in
System_DATA = $(System_files)

CLEANFILES = gdmsetup.desktop gdmsetup.desktop.in gdmflexiserver.desktop gdmflexiserver-xnest.desktop gdmphotosetup.desktop

gdmsetup.desktop.in: $(srcdir)/gdmsetup.desktop.in.in
	sed -e 's,[@]sbindir[@],$(sbindir),g' <$(srcdir)/gdmsetup.desktop.in.in \
		>gdmsetup.desktop.in

@INTLTOOL_DESKTOP_RULE@

Gladedir = $(datadir)/gdm
Glade_DATA = gdmsetup.glade gdmchooser.glade gdmphotosetup.glade

Iconsdir = $(datadir)/pixmaps

Icons_DATA = \
	login-photo.png

install-data-local:
	-test -z "$(DESTDIR)$(bindir)/gdmXnest" || rm -f $(DESTDIR)$(bindir)/gdmXnest
	$(LN_S) -f gdmXnestchooser $(DESTDIR)$(bindir)/gdmXnest

EXTRA_DIST = \
	$(Glade_DATA)		\
	$(Icons_DATA)		\
	$(System_DATA)		\
	$(System_dist_files)	\
	$(Settings_DATA)	\
	$(Settings_files)	\
	$(Settings_in_files)

clean-local:
	rm -f $(System_files) $(Settings_files)
