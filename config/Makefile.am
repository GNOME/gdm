pixmapdir = $(datadIR)/pixmaps
confdir = $(sysconfdir)/gdm
localedir = $(sysconfdir)/gdm
sessdir = $(sysconfdir)/gdm/Sessions
initdir = $(sysconfdir)/gdm/Init
authdir = $(localstatedir)/gdm
gnomercdir = $(sysconfdir)/gdm
postdir = $(sysconfdir)/gdm/PostSession
predir = $(sysconfdir)/gdm/PreSession


noinst_DATA = gdm.conf

EXTRA_DIST = \
	gdm.conf.in \
	Gnome.in \
	gnomerc.in \
	Xsession \
	gdm \
	gdm-autologin \
	locale.alias \
	Default \
	PostSession \
	PreSession \
	Default.redhat \
	XKeepsCrashing \
	gettextfoo.h \
	extract-shell.sh

gettextfoo.h: XKeepsCrashing
	cat $^ | $(srcdir)/extract-shell.sh > gettextfoo.h

install-data-hook: gdm.conf gnomerc Gnome
	if test '!' -d $(confdir); then \
		$(mkinstalldirs) $(confdir); \
		chmod 755 $(confdir); \
	fi
	if test '!' -f $(confdir)/gdm.conf; then \
		$(INSTALL_DATA) gdm.conf $(confdir)/gdm.conf; \
	fi
	$(INSTALL_DATA) gdm.conf $(confdir)/factory-gdm.conf

	$(INSTALL_SCRIPT) $(srcdir)/XKeepsCrashing $(confdir)/XKeepsCrashing

	-if test -f $(localedir)/locale.alias; then \
		cp $(localedir)/locale.alias $(localedir)/locale.alias.orig; \
	fi
	$(INSTALL_DATA) $(srcdir)/locale.alias $(localedir)/locale.alias

	if test '!' -d $(sessdir); then \
		$(mkinstalldirs) $(sessdir); \
		chmod 755 $(sessdir); \
	fi

	-if test -f $(sessdir)/Xsession; then \
		cp $(sessdir)/Xsession $(sessdir)/Xsession.orig; \
	fi
	$(INSTALL_SCRIPT) $(srcdir)/Xsession $(sessdir)/Xsession

	-if test -f $(sessdir)/Gnome; then \
		cp $(sessdir)/Gnome $(sessdir)/Gnome.orig; \
	fi
	$(INSTALL_SCRIPT) Gnome $(sessdir)/Gnome

	-if test -f /etc/redhat-release; then \
		if test -f $(sessdir)/Default; then \
			cp $(sessdir)/Default $(sessdir)/Default.orig; \
		fi; \
		$(INSTALL_SCRIPT) $(srcdir)/Default.redhat $(sessdir)/Default; \
	fi

	if test '!' -d $(initdir); then \
		$(mkinstalldirs) $(initdir); \
		chmod 755 $(initdir); \
	fi
	-if test -f $(initdir)/Default; then \
		cp $(initdir)/Default $(initdir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) $(srcdir)/Default $(initdir)/Default

	if test '!' -d $(predir); then \
		$(mkinstalldirs) $(predir); \
		chmod 755 $(predir); \
	fi
	-if test -f $(predir)/Default; then \
		cp $(predir)/Default $(predir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) $(srcdir)/PreSession $(predir)/Default

	if test '!' -d $(postdir); then \
		$(mkinstalldirs) $(postdir); \
		chmod 755 $(postdir); \
	fi
	-if test -f $(postdir)/Default; then \
		cp $(postdir)/Default $(postdir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) $(srcdir)/PostSession $(postdir)/Default

	if test '!' -d $(gnomercdir); then \
		$(mkinstalldirs) $(gnomercdir); \
		chmod 755 $(gnomercdir); \
	fi

	if test '!' -f $(gnomercdir)/gnomerc; then \
		$(INSTALL_SCRIPT) gnomerc $(gnomercdir)/gnomerc; \
	fi

	if test '!' -d $(authdir); then \
		$(mkinstalldirs) $(authdir); \
		chown gdm.gdm $(authdir); \
		chmod 750 $(authdir); \
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
