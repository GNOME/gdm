SUBDIRS = config macros pixmaps daemon gui docs intl po

EXTRA_DIST = ChangeLog README.install RELEASENOTES gdm.spec gdm.spec.in \
             gdmconfig-pam gdmconfig-security

install-data-local:
if CONSOLE_HELPER
	$(INSTALL_DATA) -D gdmconfig-pam $(sysconfdir)/pam.d/gdmconfig
	$(INSTALL_DATA) -D gdmconfig-security $(sysconfdir)/security/console.apps/gdmconfig
	$(INSTALL_DATA) -m 755 -d $(sbindir)
	mv $(bindir)/gdmconfig $(sbindir)
	$(LN_S) @bindir@/consolehelper $(bindir)/gdmconfig
endif

