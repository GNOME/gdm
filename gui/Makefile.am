## Process this file with automake to produce makefile.in
CFLAGS += -g -Wall 	\
	-Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

DEFS += -DGDM_CONFIG_FILE=\"@sysconfdir@/gdm/gdm.conf\" \
	-DGDM_FACTORY_CONFIG_FILE=\"@sysconfdir@/gdm/factory-gdm.conf\"

INCLUDES = -I. -I.. -I$(top_srcdir)/daemon -I$(includedir)		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	@LIBGLADE_CFLAGS@				\
	@PIXBUF_CFLAGS@					\
	$(GNOME_INCLUDEDIR)

bin_PROGRAMS = gdmchooser gdmlogin gdmconfig gdmphotosetup

gdmphotosetup_SOURCES = \
	gdmphotosetup.c

gdmchooser_SOURCES = \
	gdmchooser.c		\
	gdmchooser.h		\
	misc.c			\
	misc.h

gdmlogin_SOURCES = \
	gdmlogin.c		\
	gdmlogin.h		\
	gdmlanguages.c		\
	gdmlanguages.h		\
	gdmwm.c			\
	gdmwm.h			\
	misc.c			\
	misc.h

gdmconfig_SOURCES = \
	icon-entry-hack.c	\
	icon-entry-hack.h	\
	gdmconfig.c		\
	gdmconfig.h		\
	misc.c			\
	misc.h

gdmchooser_LDADD = $(GNOME_LIBDIR) $(GNOME_LIBS) $(GNOMEUI_LIBS) -lXdmcp -lX11 $(INTLLIBS) @LIBGLADE_LIBS@
gdmlogin_LDADD = $(GNOME_LIBDIR) $(GNOME_LIBS) $(GNOMEUI_LIBS) $(INTLLIBS) @PIXBUF_LIBS@
gdmconfig_LDADD = $(GNOME_LIBDIR) $(GNOME_LIBS) $(GNOMEUI_LIBS) $(INTLLIBS) @LIBGLADE_LIBS@
gdmphotosetup_LDADD = $(GNOME_LIBDIR) $(GNOME_LIBS) $(GNOMEUI_LIBS) $(INTLLIBS)
 
Settingsdir = $(datadir)/gnome/apps/Settings
Settings_in_files = gdmphotosetup.desktop.in
Settings_DATA = $(Settings_in_files:.desktop.in=.desktop)
Systemdir = $(datadir)/gnome/apps/System
System_in_files = gdmconfig.desktop.in
System_DATA = $(System_in_files:.desktop.in=.desktop)
@XML_I18N_MERGE_DESKTOP_RULE@

Gladedir = $(datadir)/gdm
Glade_DATA = gdmconfig.glade gdmchooser.glade

EXTRA_DIST = \
	gdmconfig-strings.c	\
	gdmchooser-strings.c	\
	$(Glade_DATA)		\
	$(System_DATA)		\
	$(System_in_files)	\
	$(Settings_DATA)	\
	$(Settings_in_files)
