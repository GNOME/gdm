SUBDIRS = config pixmaps vicious-extensions \
	daemon gui utils po 

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
	gdm-restart.in		\
	gdm-safe-restart.in	\
	gdmconfig

sbin_SCRIPTS = gdm-restart gdm-safe-restart gdmconfig

install-data-local: gdmsetup-security
if CONSOLE_HELPER
	$(INSTALL_DATA) -D $(srcdir)/gdmsetup-pam $(DESTDIR)$(PAM_PREFIX)/pam.d/gdmsetup
	$(INSTALL_DATA) -D gdmsetup-security $(DESTDIR)$(PAM_PREFIX)/security/console.apps/gdmsetup
	$(INSTALL_DATA) -m 755 -d $(sbindir)
	mv -f $(bindir)/gdmsetup $(sbindir)
	$(LN_S) @CONSOLE_HELPER@ $(bindir)/gdmsetup
endif

DISTCLEANFILES = \
	intltool-extract	\
	intltool-extract.in	\
	intltool-merge		\
	intltool-merge.in	\
	intltool-update		\
	intltool-update.in
