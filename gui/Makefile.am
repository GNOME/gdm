## Process this file with automake to produce makefile.in
CFLAGS += -g -Wall 	\
	-Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

DEFS += -DGDM_CONFIG_FILE=\"@sysconfdir@/gdm/gdm.conf\"

INCLUDES = -I. -I.. -I$(top_srcdir)/daemon -I$(includedir)		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DLOCALEDIR=\"$(datadir)/locale\"		\
	$(GNOME_INCLUDEDIR)

bin_PROGRAMS = gdmchooser gdmlogin

gdmchooser_SOURCES = \
	gdmchooser.c \
	gdmchooser.h 

gdmlogin_SOURCES = \
	gdmlogin.c \
	filecheck.c

gdmchooser_LDADD = $(GNOME_LIBDIR) $(GNOME_LIBS) $(GNOMEUI_LIBS) -lXdmcp -lX11 $(INTLLIBS)
gdmlogin_LDADD = $(GNOME_LIBDIR) $(GNOME_LIBS) $(GNOMEUI_LIBS) $(INTLLIBS)

