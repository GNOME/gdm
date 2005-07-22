SUBDIRS = . greeter modules faces

## Process this file with automake to produce makefile.in
DEFS = @DEFS@ \
	-DPREFIX=\"@prefix@\" \
	-DSYSCONFDIR=\"@sysconfdir@\" \
	-DDATADIR=\"@datadir@\" \
	-DLIBDIR=\"@libdir@\" \
	-DGDM_CONFIG_FILE=\"@sysconfdir@/gdm/gdm.conf\" \
	-DGDM_FACTORY_CONFIG_FILE=\"@sysconfdir@/gdm/factory-gdm.conf\" \
	-DGDM_GLADE_DIR=\"@datadir@/gdm\"

INCLUDES = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/daemon				\
	-I$(top_srcdir)/vicious-extensions		\
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
	gdmXnestchooser.c	\
	gdmcomm.c		\
	gdmcomm.h

gdmflexiserver_SOURCES = \
	gdmflexiserver.c	\
	gdmcomm.c		\
	gdmcomm.h

gdmdynamic_SOURCES = \
	gdmdynamic.c \
	gdmcomm.c    \
	gdmcomm.h

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
	gdmcommon.c		\
	gdmcommon.h

gdmchooser_SOURCES = \
	gdmchooser.c		\
	misc.c			\
	misc.h			

gdmlogin_SOURCES = \
	gdmlogin.c

gdmsetup_SOURCES = \
	gdmsetup.c		\
	gdmcomm.c		\
	gdmcomm.h

gdmchooser_LDFLAGS = -export-dynamic
gdmchooser_LDADD = \
	$(EXTRA_CHOOSER_LIBS)	\
	libgdmwm.a		\
	libgdmcommon.a		\
	$(GUIGLADE_LIBS)	\
	$(X_LIBS)		\
	$(XDMCP_LIBS)		\
	-lpopt			\
	-lX11			\
	$(XINERAMA_LIBS)	\
	$(X_EXTRA_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(PANGO_LIBS)		\
	$(GDK_LIBS)		\
	$(GDKPIXBUF_LIBS)	\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui

gdmlogin_LDADD = \
	libgdmwm.a		\
	libgdmcommon.a		\
	$(GUING_LIBS)		\
	$(X_LIBS)		\
	-lX11			\
	$(XINERAMA_LIBS)	\
	$(X_EXTRA_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(PANGO_LIBS)		\
	$(GDKPIXBUF_LIBS)	\
	$(LIBGNOME_LIBS)	\
	$(LIBGNOMEUI_LIBS)	\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui-minimal

gdmsetup_LDFLAGS = -export-dynamic
gdmsetup_LDADD = \
	libgdmcommon.a		\
	$(GUI_LIBS)		\
	-lXau			\
	-lX11			\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(PANGO_LIBS)		\
	$(GDK_LIBS)		\
	$(GDKPIXBUF_LIBS)	\
	$(LIBGNOME_LIBS)	\
	$(LIBGNOMEUI_LIBS)	\
	$(EXTRA_SETUP_LIBS)	\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui-gnome

gdmphotosetup_LDADD = \
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(GDKPIXBUF_LIBS)	\
	$(LIBGNOME_LIBS)	\
	$(LIBGNOMEUI_LIBS)	\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui-gnome

gdmXnestchooser_LDADD = \
	$(GUI_LIBS)		\
	-lXau			\
	-lX11			\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(LIBGNOME_LIBS)	\
	$(LIBGNOMEUI_LIBS)	\
	$(EXTRA_XNEST_LIBS)	\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui-gnome

gdmflexiserver_LDADD = \
	$(GUI_LIBS)		\
	-lXau			\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(LIBGNOME_LIBS)	\
	$(LIBGNOMEUI_LIBS)	\
	$(EXTRA_FLEXI_LIBS)	\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui-gnome

gdmdynamic_LDADD = 		\
	$(GUI_LIBS)		\
	-lXau			\
	$(INTLLIBS) 		\
	$(GLIB_LIBS) 		\
	$(GOBJECT_LIBS) 	\
	$(GDK_LIBS) 		\
	$(LIBGNOME_LIBS) 	\
	$(LIBGNOMEUI_LIBS)	\
	$(EXTRA_DYNAMIC_LIBS)	\
	-L$(top_builddir)/vicious-extensions \
	-lviciousui-gnome

Settingsdir = $(datadir)/gnome/capplets
Settings_files = gdmphotosetup.desktop
Settings_in_files = gdmphotosetup.desktop.in
Settings_DATA = $(Settings_in_files:.desktop.in=.desktop)

Systemdir = $(datadir)/applications
System_files = \
	gdmsetup.desktop	\
	gdmflexiserver.desktop	\
	gdmflexiserver-xnest.desktop
System_in_files = \
	gdmsetup.desktop.in		\
	gdmflexiserver.desktop.in	\
	gdmflexiserver-xnest.desktop.in
System_DATA = $(System_in_files:.desktop.in=.desktop)

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
	$(System_files)		\
	$(System_in_files)	\
	$(Settings_DATA)	\
	$(Settings_files)	\
	$(Settings_in_files)

clean-local:
	rm -f $(System_files) $(Settings_files)
