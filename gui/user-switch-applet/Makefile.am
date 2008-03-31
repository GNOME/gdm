NULL =

INCLUDES = \
	-I$(top_srcdir)/gui/simple-greeter		\
	-DPREFIX=\""$(prefix)"\" 			\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DDATADIR=\""$(datadir)"\"		 	\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DLIBLOCALEDIR=\""$(prefix)/lib/locale"\"	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DGLADEDIR=\""$(pkgdatadir)"\" 			\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	$(GTK_CFLAGS)					\
	$(APPLET_CFLAGS)				\
	$(NULL)

libexec_PROGRAMS =			\
	gdm-user-switch-applet		\
	$(NULL)

gdm_user_switch_applet_SOURCES =  	\
	applet.c 			\
	gdm-user-menu-item.h		\
	gdm-user-menu-item.c		\
	$(NULL)

gdm_user_switch_applet_LDADD = 		\
	$(top_builddir)/gui/simple-greeter/libgdmuser.la	\
	$(COMMON_LIBS)					\
	$(APPLET_LIBS)					\
	$(NULL)

# Bonobo UI
uidir =	$(datadir)/gnome-2.0/ui
ui_DATA =					\
	GNOME_FastUserSwitchApplet.xml		\
	$(NULL)

# Bonobo .server
serverdir = $(libdir)/bonobo/servers
server_in_files =				\
	GNOME_FastUserSwitchApplet.server.in	\
	$(NULL)
server_DATA = $(server_in_files:.server.in=.server)

$(server_in_files): $(server_in_files:.server.in=.server.in.in)
	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" -e "s|\@VERSION\@|$(VERSION)|" $< > $@

@INTLTOOL_SERVER_RULE@

EXTRA_DIST = 			\
	$(ui_DATA)		\
	GNOME_FastUserSwitchApplet.server.in.in	\
	$(NULL)

DISTCLEANFILES =                        \
	$(server_in_files)		\
	$(server_DATA)			\
        $(NULL)

MAINTAINERCLEANFILES =                  \
        *~                              \
        Makefile.in
