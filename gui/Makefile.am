NULL =

SUBDIRS =
	. 		\
	greeter		\
	modules		\
	faces		\
	$(NULL)

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
	$(GUI_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(NULL)

#
#	-DG_DISABLE_DEPRECATED				\
#	-DGDK_DISABLE_DEPRECATED			\
#	-DGDK_PIXBUF_DISABLE_DEPRECATED			\
#	-DGTK_DISABLE_DEPRECATED			\
#	-DGNOME_DISABLE_DEPRECATED			\
#

noinst_LIBRARIES =		\
	libgdmwm.a		\
	libgdmguicommon.a	\
	$(NULL)

libexec_PROGRAMS = 		\
	@GDMCHOOSER@ 		\
	$(NULL)

bin_PROGRAMS = \
	@GDMXNESTCHOOSER@	\
	gdmflexiserver      	\
	$(NULL)

EXTRA_PROGRAMS = 		\
	gdmchooser		\
	gdmXnestchooser		\
	$(NULL)

gdmXnestchooser_SOURCES = 	\
	gdmXnestchooser.c	\
	$(NULL)

gdmflexiserver_SOURCES = 	\
	gdmflexiserver.c	\
	$(NULL)

libgdmwm_a_SOURCES = \
	gdmwm.c			\
	gdmwm.h			\
	$(NULL)

libgdmguicommon_a_SOURCES = 	\
	gdm-greeter.c		\
	gdm-greeter.h		\
	gdmlanguages.c		\
	gdmlanguages.h		\
	gdmuser.c		\
	gdmuser.h		\
	gdmsession.c		\
	gdmsession.h		\
	misc.c			\
	misc.h			\
	gdmcommon.c		\
	gdmcommon.h		\
	$(NULL)

gdmchooser_SOURCES = 		\
	gdmchooser.c		\
	misc.c			\
	misc.h			\
	$(NULL)

gdmchooser_LDFLAGS = -export-dynamic
gdmchooser_LDADD = 		\
	$(EXTRA_CHOOSER_LIBS)	\
	libgdmwm.a		\
	libgdmguicommon.a	\
	$(GUIGLADE_LIBS)	\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(DBUS_LIBS)		\
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
	$(NULL)

gdmXnestchooser_LDADD = 	\
	libgdmguicommon.a	\
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(DBUS_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(top_builddir)/common/libgdmcommon.a \
	$(EXTRA_XNEST_LIBS)	\
	$(X_LIBS)		\
	-lXau			\
	-lX11			\
	$(NULL)

gdmflexiserver_LDADD = 		\
	libgdmguicommon.a	\
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	$(GLIB_LIBS)		\
	$(DBUS_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GDK_LIBS)		\
	$(top_builddir)/common/libgdmcommon.a \
	$(EXTRA_FLEXI_LIBS)	\
	$(X_LIBS)		\
	-lX11			\
	-lXau			\
	$(NULL)

Systemdir = $(datadir)/applications
System_files = \
	gdmflexiserver.desktop		\
	gdmflexiserver-xnest.desktop	\
	$(NULL)

System_dist_files = \
	gdmflexiserver.desktop.in		\
	gdmflexiserver-xnest.desktop.in.in	\
	$(NULL)

System_DATA = $(System_files)

CLEANFILES = 				\
	gdmflexiserver.desktop		\
	gdmflexiserver-xnest.desktop	\
	gdmflexiserver-xnest.desktop.in	\
	$(BUILT_SOURCES)		\
	$(NULL)

@INTLTOOL_DESKTOP_RULE@

Gladedir = $(datadir)/gdm
Glade_DATA = 			\
	gdmchooser.glade	\
	$(NULL)

Iconsdir = $(datadir)/pixmaps

Icons_DATA =			\
	login-photo.png		\
	$(NULL)

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
