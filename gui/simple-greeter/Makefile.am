NULL =

INCLUDES = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/libgreeter			\
	-DAUTHDIR=\""$(authdir)"\"		 	\
	-DDATADIR=\""$(datadir)"\"		 	\
	-DGDMCONFDIR=\"$(gdmconfdir)\"			\
	-DDMCONFDIR=\""$(dmconfdir)"\"		 	\
	-DGDM_CONFIG_FILE=\"$(gdmconfdir)/gdm.conf\"	\
	-DGDMLOCALEDIR=\""$(gdmlocaledir)"\"		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGLADEDIR=\""$(pkgdatadir)"\" 	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	-DPIXMAPDIR=\""$(pixmapdir)"\"		 	\
	$(GUI_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(GREETER_CFLAGS)				\
	$(NULL)

libexec_PROGRAMS =					\
	gdm-simple-greeter

gdm_simple_greeter_SOURCES =  	\
	greeter-main.c 		\
	greeter-main.h 		\
	gdm-simple-greeter.c	\
	gdm-simple-greeter.h	\
	$(NULL)

gdm_simple_greeter_LDADD = 		\
	$(EXTRA_GREETER_LIBS)   	\
	-L$(top_builddir)/libgreeter	\
	-lgreeter			\
	$(GLIB_LIBS)		\
	$(DBUS_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(GUI_LIBS)		\
	$(GREETER_LIBS)		\
	$(NULL)

gladedir = $(pkgdatadir)
glade_DATA = 				\
	gdm-simple-greeter.glade	\
	$(NULL)
