SUBDIRS = . themes

## Process this file with automake to produce makefile.in
CFLAGS = @CFLAGS@ -g -Wall 	\
	-Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

INCLUDES = \
	-I.						\
	-DG_DISABLE_DEPRECATED				\
	-DGDK_DISABLE_DEPRECATED			\
	-DGDK_PIXBUF_DISABLE_DEPRECATED			\
	-DGTK_DISABLE_DEPRECATED			\
	-DGNOME_DISABLE_DEPRECATED			\
	-I$(top_srcdir)/gui				\
	-I$(top_srcdir)/daemon				\
	-I$(top_srcdir)/vicious-extensions		\
	-DGDM_CONFIG_FILE=\"@sysconfdir@/gdm/gdm.conf\" \
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGREETERTHEMEDIR=\""$(datadir)/gdm/themes"\" 	\
	$(GREETER_CFLAGS)

bin_PROGRAMS = \
	gdmgreeter

bin_SCRIPTS = \
	gdmthemetester

gdmgreeter_SOURCES =  \
	greeter.c \
	greeter.h \
	greeter_action_language.c \
	greeter_action_language.h \
	greeter_canvas_item.c \
	greeter_canvas_item.h \
	greeter_configuration.h \
	greeter_events.c \
	greeter_events.h \
	greeter_geometry.c \
	greeter_geometry.h \
	greeter_item.c \
	greeter_item_timed.c \
	greeter_item_timed.h \
	greeter_item_capslock.c \
	greeter_item_capslock.h \
	greeter_item_clock.c \
	greeter_item_clock.h \
	greeter_item.h \
	greeter_item_pam.c \
	greeter_item_pam.h \
	greeter_parser.c \
	greeter_parser.h \
	greeter_session.c \
	greeter_session.h \
	greeter_system.c \
	greeter_system.h 

gdmgreeter_LDADD = \
	-L$(top_builddir)/gui	\
	-lgdmwm			\
	-lgdmlang		\
	-L$(top_builddir)/vicious-extensions	\
	-lvicious		\
	$(X_LIBS)		\
	-lX11			\
	$(XINERAMA_LIBS)	\
	$(X_EXTRA_LIBS)		\
	$(GREETER_LIBS)

EXTRA_DIST = \
	gdmthemetester
