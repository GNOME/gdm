uidir = $(datadir)/gdm/initial-setup

# GNOMECC_DATA_DIR is used in tz.c
# DATADIR is used in cc-timezone-map.c
# these two files are copied from gnome-control-center,
# which explains the slightly odd locations
AM_CPPFLAGS = \
	$(INITIAL_SETUP_CFLAGS) \
	-DUIDIR="\"$(uidir)\"" \
	-DGNOMECC_DATA_DIR="\"$(datadir)/gnome-control-center\"" \
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-DDATADIR="\"$(datadir)/gnome-control-center/ui/datetime\"" \
	-I../libgdmgreeter

LIBS = $(INITIAL_SETUP_LIBS) -lm ../libgdmgreeter/libgdmgreeter.la

libexec_PROGRAMS = gdm-initial-setup

dbus_built_sources = timedated.c timedated.h
timedated.c: timedated.h
timedated.h: Makefile.am timedated1-interface.xml
	gdbus-codegen \
		--interface-prefix org.freedesktop. \
		--generate-c-code timedated \
		$(srcdir)/timedated1-interface.xml

setup_resources.c: setup.gresource.xml setup.ui welcome-image.jpg
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate-source $(srcdir)/setup.gresource.xml

gdm_initial_setup_SOURCES =	\
	gdm-initial-setup.c \
	panel-cell-renderer-signal.c panel-cell-renderer-signal.h \
	panel-cell-renderer-mode.c panel-cell-renderer-mode.h \
	panel-cell-renderer-security.c panel-cell-renderer-security.h \
	cc-timezone-map.c cc-timezone-map.h \
	um-utils.c um-utils.h \
	um-crop-area.c um-crop-area.h \
	um-photo-dialog.c um-photo-dialog.h \
	tz.c tz.h \
	../libgdmgreeter/gdm-greeter-client.c ../libgdmgreeter/gdm-greeter-client.h \
	$(dbus_built_sources) \
	setup_resources.c


EXTRA_DIST = \
	$(sys_DATA) \
	timedated1-interface.xml \
	setup.ui \
	welcome-image.jpg \
	application.gresource.xml

BUILT_SOURCES = $(dbus_built_sources) setup_resources.c

-include $(top_srcdir)/git.mk
