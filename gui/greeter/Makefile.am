INCLUDES = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/daemon				\
	-I$(top_srcdir)/vicious-extensions		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(GREETER_CFLAGS)

bin_PROGRAMS = \
	gdmgreeter

gdmgreeter_SOURCES =  \
	greeter.c \
	greeter_action_language.c \
	greeter_action_language.h \
	greeter_canvas_item.c \
	greeter_canvas_item.h \
	greeter_dialogs.c \
	greeter_dialogs.h \
	greeter_events.c \
	greeter_events.h \
	greeter_geometry.c \
	greeter_geometry.h \
	greeter_item.c \
	greeter_item_capslock.c \
	greeter_item_capslock.h \
	greeter_item_clock.c \
	greeter_item_clock.h \
	greeter_item.h \
	greeter_item_pam.c \
	greeter_item_pam.h \
	greeter_parser.c \
	greeter_parser.h

gdmgreeter_LDADD = \
	../libgdmwm.a 		\
	$(XINERAMA_LIBS)	\
	$(GREETER_LIBS)

EXTRA_DIST = \
	greeter_lang_list.c
