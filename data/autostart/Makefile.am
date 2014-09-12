NULL =

%.desktop: %.desktop.in Makefile
	$(AM_V_GEN) sed -e 's|@LIBEXECDIR[@]|$(libexecdir)|g' $< >$@

desktopdir = $(pkgdatadir)/greeter/autostart
desktop_in_files =							\
	caribou-autostart.desktop.in					\
	$(NULL)
desktop_DATA = orca-autostart.desktop					\
	       $(desktop_in_files:.desktop.in=.desktop)			\
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
