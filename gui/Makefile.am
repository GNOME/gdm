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
	-I$(top_srcdir)/daemon				\
	-I$(top_srcdir)/vicious-extensions		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(GUI_CFLAGS)

bin_PROGRAMS = \
	@GDMCHOOSER@ 		\
	gdmlogin 		\
	gdmconfig 		\
	gdmphotosetup 		\
	@GDMXNESTCHOOSER@	\
	gdmflexiserver
EXTRA_PROGRAMS = gdmchooser gdmXnestchooser

gdmphotosetup_SOURCES = \
	gdmphotosetup.c

gdmXnestchooser_SOURCES = \
	gdmXnestchooser.c

gdmflexiserver_SOURCES = \
	gdmflexiserver.c

gdmchooser_SOURCES = \
	gdmchooser.c		\
	misc.c			\
	misc.h			\
	gdmwm.c			\
	gdmwm.h

gdmlogin_SOURCES = \
	gdmlogin.c		\
	gdmlanguages.c		\
	gdmlanguages.h		\
	gdmwm.c			\
	gdmwm.h			\
	misc.c			\
	misc.h

gdmconfig_SOURCES = \
	gdmconfig.c		\
	gdmconfig.h		\
	misc.c			\
	misc.h

gdmchooser_LDADD = \
	$(GUI_LIBS)		\
	$(XDMCP_LIBS)		\
	-lX11			\
	$(XINERAMA_LIBS)	\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lvicious		\
	-lviciousui

gdmlogin_LDADD = \
	$(GUI_LIBS)		\
	$(XINERAMA_LIBS)	\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lvicious		\
	-lviciousui

gdmconfig_LDADD = \
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lvicious		\
	-lviciousui

gdmphotosetup_LDADD = \
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lvicious		\
	-lviciousui

gdmXnestchooser_LDADD = \
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lvicious		\
	-lviciousui

gdmflexiserver_LDADD = \
	$(GUI_LIBS)		\
	$(INTLLIBS)		\
	-L$(top_builddir)/vicious-extensions	\
	-lvicious		\
	-lviciousui
 
Settingsdir = $(datadir)/gnome/apps/Settings
Settings_in_files = gdmphotosetup.desktop.in
Settings_DATA = $(Settings_in_files:.desktop.in=.desktop)

Systemdir = $(datadir)/gnome/apps/System
System_in_files = \
	gdmconfig.desktop.in		\
	gdmflexiserver.desktop.in	\
	gdmflexiserver-xnest.desktop.in
System_DATA = $(System_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

Gladedir = $(datadir)/gdm
Glade_DATA = gdmconfig.glade gdmchooser.glade

EXTRA_DIST = \
	gdmconfig-strings.c	\
	gdmchooser-strings.c	\
	$(Glade_DATA)		\
	$(System_DATA)		\
	$(System_in_files)	\
	$(Settings_DATA)	\
	$(Settings_in_files)	\
	gdmchooser.c		\
	gdmchooser.h
