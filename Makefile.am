SUBDIRS = config macros pixmaps intl vicious-extensions \
	daemon gui utils docs omf-install po

EXTRA_DIST = \
	ChangeLog		\
	README.install		\
	gdm.spec		\
	gdm.spec.in		\
	gdmconfig-pam		\
	gdmconfig-security.in	\
	xml-i18n-extract.in	\
	xml-i18n-merge.in	\
	xml-i18n-update.in	\
	gdm-restart.in		\
	gdm-safe-restart.in

sbin_SCRIPTS = gdm-restart gdm-safe-restart

install-data-local: gdmconfig-security
if CONSOLE_HELPER
	$(INSTALL_DATA) -D $(srcdir)/gdmconfig-pam $(DESTDIR)$(PAM_PREFIX)/pam.d/gdmconfig
	$(INSTALL_DATA) -D gdmconfig-security $(DESTDIR)$(PAM_PREFIX)/security/console.apps/gdmconfig
	$(INSTALL_DATA) -m 755 -d $(sbindir)
	mv -f $(bindir)/gdmconfig $(sbindir)
	$(LN_S) @CONSOLE_HELPER@ $(bindir)/gdmconfig
endif

DISTCLEANFILES = \
	xml-i18n-extract	\
	xml-i18n-extract.in	\
	xml-i18n-merge		\
	xml-i18n-merge.in	\
	xml-i18n-update		\
	xml-i18n-update.in
