uidir = $(datadir)/gdm/initial-setup

# GNOMECC_DATA_DIR is used in tz.c
# DATADIR is used in cc-timezone-map.c
# these two files are copied from gnome-control-center,
# which explains the slightly odd locations
AM_CPPFLAGS = \
	$(INITIAL_SETUP_CFLAGS) \
	-DUIDIR="\"$(uidir)\"" \
	-DGNOMECC_DATA_DIR="\"$(datadir)/gnome-control-center\"" \
	-DDATADIR="\"$(datadir)/gnome-control-center/ui/datetime\""

LIBS = $(INITIAL_SETUP_LIBS) -lm

libexec_PROGRAMS = gdm-initial-setup

gdm_initial_setup_SOURCES =	\
	gdm-initial-setup.c \
	panel-cell-renderer-signal.c panel-cell-renderer-signal.h \
	panel-cell-renderer-mode.c panel-cell-renderer-mode.h \
	panel-cell-renderer-security.c panel-cell-renderer-security.h \
	cc-timezone-map.c cc-timezone-map.h \
	um-utils.c um-utils.h \
	tz.c tz.h \
	dtm.c dtm.h

ui_DATA = \
	setup.ui \
	welcome-image.jpg

EXTRA_DIST = \
	$(ui_DATA) \
	$(sys_DATA)

-include $(top_srcdir)/git.mk
