NULL =

SUBDIRS = 				\
	libnotificationarea		\
	$(NULL)

INCLUDES = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/libgreeter			\
	-I$(top_srcdir)/gui/simple-greeter/libnotificationarea	\
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

noinst_PROGRAMS = 		\
	test-greeter-panel	\
	$(NULL)

test_greeter_panel_SOURCES = 	\
	test-greeter-panel.c	\
	gdm-greeter-panel.h	\
	gdm-greeter-panel.c	\
	$(NULL)

test_greeter_panel_LDADD =	\
	$(top_builddir)/gui/simple-greeter/libnotificationarea/libnotificationarea.la	\
	$(GUI_LIBS)		\
	$(NULL)

libexec_PROGRAMS =					\
	gdm-simple-greeter

gdm_simple_greeter_SOURCES =  	\
	greeter-main.c 		\
	gdm-simple-greeter.c	\
	gdm-simple-greeter.h	\
	gdm-greeter-panel.h	\
	gdm-greeter-panel.c	\
	gdm-greeter-background.h\
	gdm-greeter-background.c\
	$(NULL)

gdm_simple_greeter_LDADD = 		\
	$(EXTRA_GREETER_LIBS)   	\
	-L$(top_builddir)/libgreeter	\
	-lgreeter			\
	$(top_builddir)/gui/simple-greeter/libnotificationarea/libnotificationarea.la	\
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

EXTRA_DIST = 			\
	$(glade_DATA)		\
	$(NULL)
