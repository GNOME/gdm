## Process this file with automake to produce makefile.in
CFLAGS += -g -Wall 	\
	-Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

DEFS += -DGDM_CONFIG_FILE=\"@sysconfdir@/gdm/gdm.conf\" \
	-DGDM_FACTORY_CONFIG_FILE=\"@sysconfdir@/gdm/factory-gdm.conf\" \
	-DGDM_GLADE_DIR=\"@datadir@/gdm\"

INCLUDES = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/daemon				\
	-I$(top_srcdir)/vicious-extensions		\
	-I$(includedir)					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	@LIBGLADE_CFLAGS@				\
	@PIXBUF_CFLAGS@					\
	$(GNOME_CFLAGS)					\
	$(GNOME_INCLUDEDIR)

sbin_PROGRAMS = \
	@GDMASKPASS@ \
	@GDMOPEN@
EXTRA_PROGRAMS = gdmaskpass gdmopen

gdmaskpass_SOURCES = \
	gdmaskpass.c

gdmopen_SOURCES = \
	gdmopen.c

gdmaskpass_LDADD = \
	$(GNOME_LIBDIR)		\
	$(INTLLIBS)		\
	-lpam			\
	-lpam_misc

gdmopen_LDADD = \
	$(GNOME_LIBDIR)		\
	$(INTLLIBS)
