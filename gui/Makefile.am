SUBDIRS = . greeter

## Process this file with automake to produce makefile.in
CFLAGS = @CFLAGS@ -g -Wall 	\
	-Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

DEFS = @DEFS@ -DGDM_CONFIG_FILE=\"@sysconfdir@/gdm/gdm.conf\" \
	-DGDM_FACTORY_CONFIG_FILE=\"@sysconfdir@/gdm/factory-gdm.conf\" \
	-DGDM_GLADE_DIR=\"@datadir@/gdm\"


INCLUDES = \
	-I.						\
	-I..						\
	-DG_DISABLE_DEPRECATED				\
	-DGDK_DISABLE_DEPRECATED			\
	-DGDK_PIXBUF_DISABLE_DEPRECATED			\
	-DGTK_DISABLE_DEPRECATED			\
	-DGNOME_DISABLE_DEPRECATED			\
	-I$(top_srcdir)/daemon				\
	-I$(top_srcdir)/vicious-extensions		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(GUI_CFLAGS)

noinst_LIBRARIES = libgdmwm.a libgdmlang.a

bin_PROGRAMS = \
	@GDMCHOOSER@ 		\
	gdmlogin 		\
	gdmsetup 		\
	gdmphotosetup 		\
	@GDMXNESTCHOOSER@	\
	gdmflexiserver
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

libgdmwm_a_SOURCES = \
	gdmwm.c			\
	gdmwm.h

libgdmlang_a_SOURCES = \
	gdmlanguages.c		\
	gdmlanguages.h

gdmchooser_SOURCES = \
	gdmchooser.c		\
	misc.c			\
	misc.h			

gdmlogin_SOURCES = \
	gdmlogin.c		\
	misc.c			\
	misc.h

gdmsetup_SOURCES = \
	gdmsetup.c		\
	gdmcomm.c		\
	gdmcomm.h		\
	misc.c			\
	misc.h

gdmchooser_LDFLAGS = -export-dynamic
gdmchooser_LDADD = \
	libgdmwm.a		\
	$(GUI_LIBS)		\
	$(X_LIBS)		\
	$(XDMCP_LIBS)		\
	-lX11			\
	$(XINERAMA_LIBS)	\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui

gdmlogin_LDADD = \
	libgdmwm.a		\
	libgdmlang.a		\
	$(GUI_LIBS)		\
	$(X_LIBS)		\
	-lX11			\
	$(XINERAMA_LIBS)	\
	$(X_EXTRA_LIBS)		\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui

gdmsetup_LDFLAGS = -export-dynamic
gdmsetup_LDADD = \
	$(GUI_LIBS)		\
	-lXau			\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui

gdmphotosetup_LDADD = \
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui

gdmXnestchooser_LDADD = \
	$(GUI_LIBS)		\
	-lXau			\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui

gdmflexiserver_LDADD = \
	$(GUI_LIBS)		\
	-lXau			\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui

Settingsdir = $(datadir)/gnome/capplets
Settings_in_files = gdmphotosetup.desktop.in
Settings_DATA = $(Settings_in_files:.desktop.in=.desktop)

Systemdir = $(datadir)/applications
System_in_files = \
	gdmsetup.desktop.in		\
	gdmflexiserver.desktop.in	\
	gdmflexiserver-xnest.desktop.in
System_DATA = $(System_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

Gladedir = $(datadir)/gdm
Glade_DATA = gdmsetup.glade gdmchooser.glade

Iconsdir = $(datadir)/pixmaps

Icons_DATA = \
	login-photo.png

install-data-local:
	$(LN_S) -f gdmXnestchooser $(DESTDIR)$(bindir)/gdmXnest

EXTRA_DIST = \
	gdmsetup-strings.c	\
	gdmchooser-strings.c	\
	gdmsetup.gladep		\
	gdmchooser.gladep	\
	$(Glade_DATA)		\
	$(Icons_DATA)		\
	$(System_DATA)		\
	$(System_in_files)	\
	$(Settings_DATA)	\
	$(Settings_in_files)
