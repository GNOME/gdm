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
	gnome-shell.desktop.in						\
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)			\
	       mime-dummy-handler.desktop				\
	       mimeapps.list						\
	       $(NULL)

EXTRA_DIST =								\
	$(desktop_in_files)						\
	$(desktop_DATA)							\
	$(NULL)

DISTCLEANFILES =							\
	$(desktop_in_files:.desktop.in=.desktop)			\
	$(NULL)

MAINTAINERCLEANFILES =                  				\
        *~                              				\
        Makefile.in
