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
desktopdir = $(pkgdatadir)/greeter/applications
desktop_in_files = 							\
	gdm-simple-greeter.desktop.in					\
	gnome-shell.desktop.in						\
	gnome-mag.desktop.in						\
	gok.desktop.in							\
	orca-screen-reader.desktop.in					\
	nm-applet.desktop.in						\
	gnome-keyring-secrets.desktop.in				\
	gdm-initial-setup.desktop.in \
	setup-shell.desktop.in \
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)			\
	       mime-dummy-handler.desktop				\
	       mimeapps.list						\
	       $(NULL)

EXTRA_DIST =								\
	gdm-simple-greeter.desktop.in.in				\
	nm-applet.desktop.in.in						\
	gnome-keyring-secrets.desktop.in.in				\
	$(desktop_in_files)						\
	$(desktop_DATA)							\
	$(NULL)

DISTCLEANFILES =							\
	$(desktop_in_files:.desktop.in=.desktop)			\
	gdm-simple-greeter.desktop.in					\
	nm-applet.desktop.in						\
	gnome-keyring-secrets.desktop.in				\
	$(NULL)

MAINTAINERCLEANFILES =                  				\
        *~                              				\
        Makefile.in
