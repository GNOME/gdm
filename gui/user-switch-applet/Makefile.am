NULL =

INCLUDES = \
	-I$(top_srcdir)/gui/simple-greeter		\
	-DPREFIX=\""$(prefix)"\" 			\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DDATADIR=\""$(datadir)"\"		 	\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DLIBLOCALEDIR=\""$(prefix)/lib/locale"\"	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DGLADEDIR=\""$(pkgdatadir)"\" 	\
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
	$(APPLET_LIBS)					\
	$(NULL)

schemasdir   = @GCONF_SCHEMA_FILE_DIR@
schemas_in_files = gdm-user-switch-applet.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

gladedir = $(pkgdatadir)
glade_DATA = 				\
	gdm-user-switch-applet.glade	\
	$(NULL)

# Bonobo UI
uidir =	$(datadir)/gnome-2.0/ui
ui_DATA =					\
	GNOME_GdmUserSwitchApplet.xml		\
	$(NULL)

# Bonobo .server
serverdir = $(libdir)/bonobo/servers
server_in_files =				\
	GNOME_GdmUserSwitchApplet.server.in	\
	$(NULL)
server_DATA = $(server_in_files:.server.in=.server)

$(server_in_files): $(server_in_files:.server.in=.server.in.in)
	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" -e "s|\@VERSION\@|$(VERSION)|" $< > $@

@INTLTOOL_SERVER_RULE@

EXTRA_DIST = 			\
	$(schemas_in_files)	\
	$(glade_DATA)		\
	$(ui_DATA)		\
	GNOME_GdmUserSwitchApplet.server.in.in	\
	$(NULL)

DISTCLEANFILES =                        \
	$(server_in_files)		\
        $(schemas_DATA)                 \
	$(server_DATA)			\
        $(NULL)

MAINTAINERCLEANFILES =                  \
        *~                              \
        Makefile.in

if GCONF_SCHEMAS_INSTALL
# don't do this if we are building in eg. rpm
install-data-local: $(schemas_DATA)
	if test -z "$(DESTDIR)" ; then \
		for p in $(schemas_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/gui/user-switch-applet/$$p ; \
		done \
	fi
else
install-data-local:
endif
