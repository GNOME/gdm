NULL =

edit = sed \
	-e 's|@SBINDIR[@]|$(sbindir)|g' \
	-e 's|@SYSCONFDIR[@]|$(sysconfdir)|g' \
	-e 's|@LIBDIR[@]|$(libdir)|g' \
	-e 's|@LIBEXECDIR[@]|$(libexecdir)|g' \
	-e 's|@LOCALSTATEDIR[@]|$(localstatedir)|g'

%.desktop.in: %.desktop.in.in Makefile
	$(edit) $< >$@

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(pkgdatadir)/autostart/LoginWindow
desktop_in_files = 	\
	at-spi-registryd-wrapper.desktop.in	\
	gdm-simple-greeter.desktop.in		\
	gnome-mag.desktop.in			\
	gnome-power-manager.desktop.in		\
	gnome-settings-daemon.desktop.in	\
	gok.desktop.in				\
	metacity.desktop.in			\
	orca-screen-reader.desktop.in		\
	polkit-gnome-authentication-agent-1.desktop.in	\
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

EXTRA_DIST = 			\
	at-spi-registryd-wrapper.desktop.in.in	\
	gdm-simple-greeter.desktop.in.in	\
	gnome-power-manager.desktop.in.in	\
	gnome-settings-daemon.desktop.in.in	\
	polkit-gnome-authentication-agent-1.desktop.in.in \
	$(desktop_in_files)	\
	$(NULL)

DISTCLEANFILES =			\
	*.desktop			\
	at-spi-registryd-wrapper.desktop.in	\
	gdm-simple-greeter.desktop.in	\
	gnome-power-manager.desktop.in	\
	gnome-settings-daemon.desktop.in	\
	polkit-gnome-authentication-agent-1.desktop.in \
	$(NULL)

MAINTAINERCLEANFILES =                  \
        *~                              \
        Makefile.in
