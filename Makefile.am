SUBDIRS = config macros pixmaps intl daemon gui docs omf-install po

EXTRA_DIST = \
	ChangeLog		\
	README.install		\
	gdm.spec		\
	gdm.spec.in		\
	gdmconfig-pam		\
	gdmconfig-security	\
	xml-i18n-extract.in	\
	xml-i18n-merge.in	\
	xml-i18n-update.in	\
	gdm-restart.in

sbin_SCRIPTS = gdm-restart

install-data-local:
if CONSOLE_HELPER
	$(INSTALL_DATA) -D gdmconfig-pam $(sysconfdir)/pam.d/gdmconfig
	$(INSTALL_DATA) -D gdmconfig-security $(sysconfdir)/security/console.apps/gdmconfig
	$(INSTALL_DATA) -m 755 -d $(sbindir)
	mv $(bindir)/gdmconfig $(sbindir)
	$(LN_S) @bindir@/consolehelper $(bindir)/gdmconfig
endif

DISTCLEANFILES = \
	xml-i18n-extract	\
	xml-i18n-extract.in	\
	xml-i18n-merge		\
	xml-i18n-merge.in	\
	xml-i18n-update		\
	xml-i18n-update.in
