NULL =

%.desktop: %.desktop.in Makefile
	$(AM_V_GEN) sed -e 's|@LIBEXECDIR[@]|$(libexecdir)|g' $< >$@

desktopdir = $(pkgdatadir)/greeter/autostart
desktop_in_files =							\
	$(NULL)
desktop_DATA = orca-autostart.desktop					\
	       $(NULL)

EXTRA_DIST =								\
	$(desktop_in_files)						\
	$(desktop_DATA)							\
	$(NULL)

DISTCLEANFILES =							\
	$(NULL)

MAINTAINERCLEANFILES =                  				\
        *~                              				\
        Makefile.in
