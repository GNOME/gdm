## Process this file with automake to produce Makefile.in

NULL =

INCLUDES = \
	-I.					\
	-I..					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	$(GLIB_CFLAGS)

noinst_LIBRARIES = 		\
	libgdmcommon.a		\
	$(null)

libgdmcommon_a_SOURCES =	\
	gdm-common.h		\
	gdm-common.c		\
	gdm-common-config.h	\
	gdm-common-config.c	\
	ve-signal.h		\
	ve-signal.c		\
	$(NULL)

libgdmcommon_ui_a_SOURCES = 	\
	$(libgdmcommon_a_SOURCES) \
	glade-helper.c		\
	glade-helper.h		\
	gdm-common-ui.c		\
	gdm-common-ui.h		\
	$(NULL)
