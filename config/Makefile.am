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
	Xsession.in \
	gdm \
	gdm-autologin \
	locale.alias \
	Default \
	PostSession \
	PreSession \
	XKeepsCrashing \
	gettextfoo.h \
	extract-shell.sh

gettextfoo.h: XKeepsCrashing
	cat $^ | $(srcdir)/extract-shell.sh > gettextfoo.h

install-data-hook: gdm.conf gnomerc Gnome Xsession
	if test '!' -d $(DESTDIR)$(confdir); then \
		$(mkinstalldirs) $(DESTDIR)$(confdir); \
		chmod 755 $(DESTDIR)$(confdir); \
	fi
	-if cmp -s $(DESTDIR)$(confdir)/gdm.conf $(DESTDIR)$(confdir)/factory-gdm.conf 2>&1 /dev/null ; then \
		cp -f $(DESTDIR)$(confdir)/gdm.conf $(DESTDIR)$(confdir)/gdm.conf.orig; \
		$(INSTALL_DATA) gdm.conf $(DESTDIR)$(confdir)/gdm.conf; \
	fi
	if test '!' -f $(DESTDIR)$(confdir)/gdm.conf; then \
		$(INSTALL_DATA) gdm.conf $(DESTDIR)$(confdir)/gdm.conf; \
	fi
	$(INSTALL_DATA) gdm.conf $(DESTDIR)$(confdir)/factory-gdm.conf

	$(INSTALL_SCRIPT) $(srcdir)/XKeepsCrashing $(DESTDIR)$(confdir)/XKeepsCrashing

	-if test -f $(DESTDIR)$(localedir)/locale.alias; then \
		cp -f $(DESTDIR)$(localedir)/locale.alias $(DESTDIR)$(localedir)/locale.alias.orig; \
	fi
	$(INSTALL_DATA) $(srcdir)/locale.alias $(DESTDIR)$(localedir)/locale.alias

	if test '!' -d $(DESTDIR)$(sessdir); then \
		$(mkinstalldirs) $(DESTDIR)$(sessdir); \
		chmod 755 $(DESTDIR)$(sessdir); \
	fi

	-if test -f $(DESTDIR)$(sessdir)/Xsession; then \
		cp -f $(DESTDIR)$(sessdir)/Xsession $(DESTDIR)$(sessdir)/Xsession.orig; \
	fi
	$(INSTALL_SCRIPT) Xsession $(DESTDIR)$(sessdir)/Xsession

	-if test -f $(DESTDIR)$(sessdir)/Gnome; then \
		cp -f $(DESTDIR)$(sessdir)/Gnome $(DESTDIR)$(sessdir)/Gnome.orig; \
	fi
	$(INSTALL_SCRIPT) Gnome $(DESTDIR)$(sessdir)/Gnome

	-if test -f /usr/bin/switchdesk; then \
		if test -f $(DESTDIR)$(sessdir)/Default; then \
			cp -f $(DESTDIR)$(sessdir)/Default $(DESTDIR)$(sessdir)/Default.orig; \
		fi; \
		$(LN_S) -f $(sessdir)/Xsession $(DESTDIR)$(sessdir)/Default; \
	fi

	if test '!' -d $(DESTDIR)$(initdir); then \
		$(mkinstalldirs) $(DESTDIR)$(initdir); \
		chmod 755 $(DESTDIR)$(initdir); \
	fi
	-if test -f $(DESTDIR)$(initdir)/Default; then \
		cp -f $(DESTDIR)$(initdir)/Default $(DESTDIR)$(initdir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) $(srcdir)/Default $(DESTDIR)$(initdir)/Default

	if test '!' -d $(DESTDIR)$(predir); then \
		$(mkinstalldirs) $(DESTDIR)$(predir); \
		chmod 755 $(DESTDIR)$(predir); \
	fi
	-if test -f $(DESTDIR)$(predir)/Default; then \
		cp -f $(DESTDIR)$(predir)/Default $(DESTDIR)$(predir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) $(srcdir)/PreSession $(DESTDIR)$(predir)/Default

	if test '!' -d $(DESTDIR)$(postdir); then \
		$(mkinstalldirs) $(DESTDIR)$(postdir); \
		chmod 755 $(DESTDIR)$(postdir); \
	fi
	-if test -f $(DESTDIR)$(postdir)/Default; then \
		cp -f $(DESTDIR)$(postdir)/Default $(DESTDIR)$(postdir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) $(srcdir)/PostSession $(DESTDIR)$(postdir)/Default

	if test '!' -d $(DESTDIR)$(gnomercdir); then \
		$(mkinstalldirs) $(DESTDIR)$(gnomercdir); \
		chmod 755 $(DESTDIR)$(gnomercdir); \
	fi

	-if test -f $(DESTDIR)$(gnomercdir)/gnomerc; then \
		cp -f $(DESTDIR)$(gnomercdir)/gnomerc $(DESTDIR)$(gnomercdir)/gnomerc.orig; \
	fi
	$(INSTALL_SCRIPT) gnomerc $(DESTDIR)$(gnomercdir)/gnomerc

	if test '!' -d $(DESTDIR)$(authdir); then \
		$(mkinstalldirs) $(DESTDIR)$(authdir); \
		chown gdm.gdm $(DESTDIR)$(authdir); \
		chmod 750 $(DESTDIR)$(authdir); \
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
