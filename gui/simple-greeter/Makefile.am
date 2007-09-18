NULL =

SUBDIRS = 				\
	libnotificationarea		\
	$(NULL)

INCLUDES = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/libgreeter			\
	-I$(top_srcdir)/gui/simple-greeter/libnotificationarea	\
	-DGDMCONFDIR=\"$(gdmconfdir)\"                  \
	-DDATADIR=\""$(datadir)"\"		 	\
	-DLIBLOCALEDIR=\""$(libdir)/locale"\"		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGLADEDIR=\""$(pkgdatadir)"\" 	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	$(GUI_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(GREETER_CFLAGS)				\
	$(CAPPLET_CFLAGS)				\
	$(NULL)

noinst_PROGRAMS = 		\
	test-greeter-background	\
	test-greeter-panel	\
	test-language-chooser	\
	$(NULL)

test_greeter_background_SOURCES = 	\
	test-greeter-background.c	\
	gdm-greeter-background.h	\
	gdm-greeter-background.c	\
	$(NULL)

test_greeter_background_LDADD =	\
	$(CAPPLET_LIBS)		\
	$(GUI_LIBS)		\
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

test_language_chooser_SOURCES = 	\
	test-language-chooser.c		\
	gdm-language-chooser-widget.h	\
	gdm-language-chooser-widget.c	\
	langinfo.h			\
	locarchive.h			\
	gdm-language-chooser-dialog.h	\
	gdm-language-chooser-dialog.c	\
	$(NULL)

test_language_chooser_LDADD =	\
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
	$(CAPPLET_LIBS)		\
	$(NULL)

gladedir = $(pkgdatadir)
glade_DATA = 				\
	gdm-simple-greeter.glade	\
	$(NULL)

EXTRA_DIST = 			\
	$(glade_DATA)		\
	$(NULL)
