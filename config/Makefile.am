pixmapdir = $(datadIR)/pixmaps
confdir = $(sysconfdir)/gdm
crossconfdir = $(sysconfdir)/dm
localedir = $(sysconfdir)/gdm
sessdir = $(sysconfdir)/dm/Sessions
initdir = $(sysconfdir)/gdm/Init
authdir = $(localstatedir)/gdm
logdir = $(localstatedir)/gdm
gnomercdir = $(sysconfdir)/gdm
postdir = $(sysconfdir)/gdm/PostSession
predir = $(sysconfdir)/gdm/PreSession
postlogindir = $(sysconfdir)/gdm/PostLogin


noinst_DATA = gdm.conf

EXTRA_DIST = \
	gdm.conf.in \
	gnome.desktop.in \
	Default.desktop.in \
	CDE.desktop.in \
	Xsession.in \
	gdm \
	gdm-autologin \
	locale.alias \
	Default.in \
	PostSession.in \
	PreSession.in \
	PostLogin \
	XKeepsCrashing \
	gettextfoo.h \
	extract-shell.sh

gettextfoo.h: XKeepsCrashing Xsession.in
	cat $^ | $(srcdir)/extract-shell.sh > gettextfoo.h

@INTLTOOL_DESKTOP_RULE@

install-data-hook: gdm.conf Xsession gnome.desktop Default.desktop CDE.desktop Default PostSession PreSession
	if test '!' -d $(DESTDIR)$(confdir); then \
		$(mkinstalldirs) $(DESTDIR)$(confdir); \
		chmod 755 $(DESTDIR)$(confdir); \
	fi
	if test '!' -d $(DESTDIR)$(crossconfdir); then \
		$(mkinstalldirs) $(DESTDIR)$(crossconfdir); \
		chmod 755 $(DESTDIR)$(crossconfdir); \
	fi
	-if cmp -s $(DESTDIR)$(confdir)/gdm.conf $(DESTDIR)$(confdir)/factory-gdm.conf > /dev/null 2>&1 ; then \
		cp -f $(DESTDIR)$(confdir)/gdm.conf $(DESTDIR)$(confdir)/gdm.conf.orig; \
		$(INSTALL_DATA) gdm.conf $(DESTDIR)$(confdir)/gdm.conf; \
	fi
	if test '!' -f $(DESTDIR)$(confdir)/gdm.conf; then \
		$(INSTALL_DATA) gdm.conf $(DESTDIR)$(confdir)/gdm.conf; \
	fi
	$(INSTALL_DATA) gdm.conf $(DESTDIR)$(confdir)/factory-gdm.conf

	$(INSTALL_SCRIPT) $(srcdir)/XKeepsCrashing $(DESTDIR)$(confdir)/XKeepsCrashing
	$(INSTALL_SCRIPT) Xsession $(DESTDIR)$(confdir)/Xsession

	-if test -f $(DESTDIR)$(localedir)/locale.alias; then \
		cp -f $(DESTDIR)$(localedir)/locale.alias $(DESTDIR)$(localedir)/locale.alias.orig; \
	fi
	$(INSTALL_DATA) $(srcdir)/locale.alias $(DESTDIR)$(localedir)/locale.alias

	if test '!' -d $(DESTDIR)$(sessdir); then \
		$(mkinstalldirs) $(DESTDIR)$(sessdir); \
		chmod 755 $(DESTDIR)$(sessdir); \
	fi

	-if test -f $(DESTDIR)$(sessdir)/Default.desktop; then \
		cp -f $(DESTDIR)$(sessdir)/Default.desktop $(DESTDIR)$(sessdir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) Default.desktop $(DESTDIR)$(sessdir)/Default.desktop

	-if test -f $(DESTDIR)$(sessdir)/gnome.desktop; then \
		cp -f $(DESTDIR)$(sessdir)/gnome.desktop $(DESTDIR)$(sessdir)/gnome.orig; \
	fi
	$(INSTALL_SCRIPT) gnome.desktop $(DESTDIR)$(sessdir)/gnome.desktop

	-if test -f /usr/dt/bin/Xsession; then \
	   if test -f $(DESTDIR)$(sessdir)/CDE.desktop; then \
		cp -f $(DESTDIR)$(sessdir)/CDE.desktop $(DESTDIR)$(sessdir)/CDE.orig; \
	   fi; \
	   $(INSTALL_SCRIPT) CDE.desktop $(DESTDIR)$(sessdir)/CDE.desktop; \
	fi

	if test '!' -d $(DESTDIR)$(initdir); then \
		$(mkinstalldirs) $(DESTDIR)$(initdir); \
		chmod 755 $(DESTDIR)$(initdir); \
	fi
	-if test -f $(DESTDIR)$(initdir)/Default; then \
		cp -f $(DESTDIR)$(initdir)/Default $(DESTDIR)$(initdir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) Default $(DESTDIR)$(initdir)/Default

	if test '!' -d $(DESTDIR)$(postlogindir); then \
		$(mkinstalldirs) $(DESTDIR)$(postlogindir); \
		chmod 755 $(DESTDIR)$(postlogindir); \
	fi
	$(INSTALL_SCRIPT) $(srcdir)/PostLogin $(DESTDIR)$(postlogindir)/Default.sample

	if test '!' -d $(DESTDIR)$(predir); then \
		$(mkinstalldirs) $(DESTDIR)$(predir); \
		chmod 755 $(DESTDIR)$(predir); \
	fi
	-if test -f $(DESTDIR)$(predir)/Default; then \
		cp -f $(DESTDIR)$(predir)/Default $(DESTDIR)$(predir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) PreSession $(DESTDIR)$(predir)/Default

	if test '!' -d $(DESTDIR)$(postdir); then \
		$(mkinstalldirs) $(DESTDIR)$(postdir); \
		chmod 755 $(DESTDIR)$(postdir); \
	fi
	-if test -f $(DESTDIR)$(postdir)/Default; then \
		cp -f $(DESTDIR)$(postdir)/Default $(DESTDIR)$(postdir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) PostSession $(DESTDIR)$(postdir)/Default

	if test '!' -d $(DESTDIR)$(gnomercdir); then \
		$(mkinstalldirs) $(DESTDIR)$(gnomercdir); \
		chmod 755 $(DESTDIR)$(gnomercdir); \
	fi

	if test '!' -d $(DESTDIR)$(logdir); then \
		$(mkinstalldirs) $(DESTDIR)$(logdir); \
		chown root.root $(DESTDIR)$(logdir); \
		chmod 755 $(DESTDIR)$(logdir); \
	fi

	if test '!' -d $(DESTDIR)$(authdir); then \
		$(mkinstalldirs) $(DESTDIR)$(authdir); \
		chown root.gdm $(DESTDIR)$(authdir); \
		chmod 1770 $(DESTDIR)$(authdir); \
	fi

	system=`uname`; \
	if test -f /usr/include/security/pam_appl.h; then \
	  if test '!' -d $(DESTDIR)$(PAM_PREFIX)/pam.d; then \
		$(mkinstalldirs) $(DESTDIR)$(PAM_PREFIX)/pam.d; \
		chmod 755 $(DESTDIR)$(PAM_PREFIX)/pam.d; \
	   fi; \
	   if test $$system = Linux && test '!' -f $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm; then \
		$(INSTALL_DATA) gdm $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm; \
	   fi; \
	   if test $$system = Linux && test '!' -f $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-autologin; then \
		$(INSTALL_DATA) gdm-autologin $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-autologin; \
	   fi; \
	   if test $$system = SunOS; then \
		echo "Please add PAM authentication for gdm and gdm-autologin in $(PAM_PREFIX)/pam.conf!"; \
	   fi; \
	fi
