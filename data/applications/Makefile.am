NULL =

edit = sed \
	-e 's|@SBINDIR[@]|$(sbindir)|g' \
	-e 's|@SYSCONFDIR[@]|$(sysconfdir)|g' \
	-e 's|@LIBDIR[@]|$(libdir)|g' \
	-e 's|@LIBEXECDIR[@]|$(libexecdir)|g' \
	-e 's|@LOCALSTATEDIR[@]|$(localstatedir)|g' \
	-e 's,[@]AUTHENTICATION_AGENT_DIR[@],$(AUTHENTICATION_AGENT_DIR),g'

%.desktop.in: %.desktop.in.in Makefile
	$(edit) $< >$@

@INTLTOOL_DESKTOP_RULE@
desktopdir = $(pkgdatadir)/greeter/applications
desktop_in_files = 							\
	gdm-simple-greeter.desktop.in					\
	gnome-shell.desktop.in						\
	polkit-gnome-authentication-agent-1.desktop.in			\
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)			\
	       mime-dummy-handler.desktop				\
	       mimeapps.list						\
	       $(NULL)

EXTRA_DIST =								\
	gdm-simple-greeter.desktop.in.in				\
	polkit-gnome-authentication-agent-1.desktop.in.in		\
	$(desktop_in_files)						\
	$(desktop_DATA)							\
	$(NULL)

DISTCLEANFILES =							\
	$(desktop_in_files:.desktop.in=.desktop)			\
	polkit-gnome-authentication-agent-1.desktop.in			\
	gdm-simple-greeter.desktop.in					\
	$(NULL)

MAINTAINERCLEANFILES =                  				\
        *~                              				\
        Makefile.in
