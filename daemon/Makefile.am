## Process this file with automake to produce makefile.in
CFLAGS += -g -Wall 	\
	-Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

DEFS += -DGDM_CONFIG_FILE=\"@sysconfdir@/gdm/gdm.conf\"

INCLUDES = -I. -I.. -I$(includedir) 			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DLOCALEDIR=\"$(datadir)/locale\"		\
	$(GNOME_INCLUDEDIR)

bin_PROGRAMS = gdm

gdm_SOURCES = \
	gdm.c	\
	gdm.h \
	slave.c \
	server.c \
	verify.c \
	misc.c \
	auth.c \
	cookie.c \
	xdmcp.c \
	filecheck.c \
	md5.c \
	md5.h 

gdm_LDADD = $(GNOME_LIBDIR) $(GNOME_LIBS) -lXdmcp -lX11 $(INTLLIBS)

