pixmapdir = $(datadir)/pixmaps
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
	locale.alias \
	Default \
	PostSession \
	PreSession \
	Default.redhat \
	Default.debian \
	XKeepsCrashing

install-data-hook: gdm.conf gnomerc Gnome
	if test '!' -d $(confdir); then \
		$(mkinstalldirs) $(confdir); \
		chmod 755 $(confdir); \
	fi
	if test '!' -f $(confdir)/gdm.conf; then \
		$(INSTALL_DATA) gdm.conf $(confdir)/gdm.conf; \
	fi

	$(INSTALL_SCRIPT) XKeepsCrashing $(confdir)/XKeepsCrashing

	if test '!' -f $(localedir)/locale.alias; then \
		$(INSTALL_DATA) $(srcdir)/locale.alias $(localedir)/locale.alias; \
	fi

	if test '!' -d $(sessdir); then \
		$(mkinstalldirs) $(sessdir); \
		chmod 755 $(sessdir); \
	fi

	if test '!' -f $(sessdir)/Xsession; then \
		$(INSTALL_SCRIPT) $(srcdir)/Xsession $(sessdir)/Xsession; \
	fi

	if test '!' -f $(sessdir)/Gnome; then \
		$(INSTALL_SCRIPT) Gnome $(sessdir)/Gnome; \
	fi

	if test -e /etc/debian_version; then \
		$(INSTALL_SCRIPT) $(srcdir)/Default.debian $(sessdir)/Default; \
	fi

	if test '!' -d $(initdir); then \
		$(mkinstalldirs) $(initdir); \
		chmod 755 $(initdir); \
	fi
	if test '!' -f $(initdir)/Default; then \
		$(INSTALL_SCRIPT) $(srcdir)/Default $(initdir)/Default; \
	fi

	if test '!' -d $(predir); then \
		$(mkinstalldirs) $(predir); \
		chmod 755 $(predir); \
	fi
	if test '!' -f $(predir)/Default; then \
		$(INSTALL_SCRIPT) $(srcdir)/PreSession $(predir)/Default; \
	fi

	if test '!' -d $(postdir); then \
		$(mkinstalldirs) $(postdir); \
		chmod 755 $(postdir); \
	fi
	if test '!' -f $(postdir)/Default; then \
		$(INSTALL_SCRIPT) $(srcdir)/PostSession $(postdir)/Default; \
	fi

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
	  if test '!' -d $(sysconfdir)/pam.d; then \
		$(mkinstalldirs) $(sysconfdir)/pam.d; \
		chmod 755 $(sysconfdir)/pam.d; \
	   fi; \
	   if test $$system = Linux && test '!' -f $(sysconfdir)/pam.d/gdm; then \
		$(INSTALL_DATA) gdm $(sysconfdir)/pam.d/gdm; \
	   fi; \
	   if test $$system = SunOS; then \
		echo "Please add PAM authentication for gdm in $(sysconfdir)/pam.conf!"; \
	   fi; \
	fi
