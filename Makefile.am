SUBDIRS = \
	po \
	config \
	pixmaps \
	vicious-extensions \
	daemon \
	gui \
	utils

# add these when help gets added back
# docs omf-install

EXTRA_DIST = \
	ChangeLog		\
	README.install		\
	gdm.spec		\
	gdm.spec.in		\
	gdmsetup-pam		\
	gdmsetup-security.in	\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	gdm-stop.in		\
	gdm-restart.in		\
	gdm-safe-restart.in	\
	gdmconfig

sbin_SCRIPTS = gdm-stop gdm-restart gdm-safe-restart gdmconfig

install-data-local: gdmsetup-security
if CONSOLE_HELPER
	$(INSTALL_DATA) -D $(srcdir)/gdmsetup-pam $(DESTDIR)$(PAM_PREFIX)/pam.d/gdmsetup
	$(INSTALL_DATA) -D gdmsetup-security $(DESTDIR)$(PAM_PREFIX)/security/console.apps/gdmsetup
	$(INSTALL_DATA) -m 755 -d $(DESTDIR)$(sbindir)
	mv -f $(DESTDIR)$(bindir)/gdmsetup $(DESTDIR)$(sbindir)
	$(LN_S) @CONSOLE_HELPER@ $(DESTDIR)$(bindir)/gdmsetup
endif

DISTCLEANFILES = \
	intltool-extract	\
	intltool-merge		\
	intltool-update
