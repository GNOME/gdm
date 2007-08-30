NULL =

INCLUDES = \
	-I$(top_srcdir)/common				\
	-DAUTHDIR=\""$(authdir)"\"		 	\
	-DDATADIR=\""$(datadir)"\"		 	\
	-DGDMCONFDIR=\"$(gdmconfdir)\"			\
	-DDMCONFDIR=\""$(dmconfdir)"\"		 	\
	-DGDMLOCALEDIR=\""$(gdmlocaledir)"\"		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	-DPIXMAPDIR=\""$(pixmapdir)"\"		 	\
	$(GUI_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(CHOOSER_CFLAGS)				\
	$(NULL)

libexec_PROGRAMS =					\
	gdm-simple-chooser

gdm_simple_chooser_SOURCES =  		\
	chooser-main.c 			\
	gdm-host-chooser-widget.c	\
	gdm-host-chooser-widget.h	\
	gdm-host-chooser-dialog.c	\
	gdm-host-chooser-dialog.h	\
	$(NULL)

gdm_simple_chooser_LDADD = 		\
	$(EXTRA_CHOOSER_LIBS)   	\
	$(GLIB_LIBS)		\
	$(DBUS_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GUI_LIBS)		\
	$(CHOOSER_LIBS)		\
	$(NULL)

EXTRA_DIST = 			\
	$(NULL)
