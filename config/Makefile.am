pixmapdir = $(datadir)/pixmaps
confdir = $(sysconfdir)/gdm
localedir = $(sysconfdir)/gdm
sessdir = $(sysconfdir)/gdm/Sessions
initdir = $(sysconfdir)/gdm/Init
authdir = $(localstatedir)/gdm
gnomercdir = $(sysconfdir)


gdm.conf: gdm.conf.in Makefile
	sed -e 's,\@datadir\@,$(datadir),g' \
	    -e 's,\@pixmapdir\@,$(pixmapdir),g' \
	    -e 's,\@bindir\@,$(bindir),g' \
	    -e 's,\@sysconfdir\@,$(sysconfdir),g' \
	    -e 's,\@localedir\@,$(localedir),g' \
	    -e 's,\@authdir\@,$(authdir),g' \
	    < $(srcdir)/gdm.conf.in > gdm.conf.tmp; \
	mv -f gdm.conf.tmp gdm.conf

Gnome: Gnome.in Makefile
	sed -e 's,\@sysconfdir\@,$(sysconfdir),g' \
	    < $(srcdir)/Gnome.in > Gnome.tmp \
	    && mv -f Gnome.tmp Gnome

gnomerc: gnomerc.in Makefile
	sed -e 's,\@bindir\@,$(bindir),g' \
	    < $(srcdir)/gnomerc.in > gnomerc.tmp \
	    && mv -f gnomerc.tmp gnomerc

noinst_SCRIPTS = Gnome gnomerc
noinst_DATA = gdm.conf

EXTRA_DIST = \
	gdm.conf.in \
	Gnome.in \
	gnomerc.in \
	Xsession \
	gdm \
	locale.alias \
	Default

install-data-hook: gdm.conf gnomerc Gnome
	if test '!' -d $(confdir); then \
		$(mkinstalldirs) $(confdir); \
		chmod 755 $(confdir); \
	fi
	if test '!' -f $(confdir)/gdm.conf; then \
		$(INSTALL_DATA) gdm.conf $(confdir)/gdm.conf; \
	fi
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
	if test '!' -d $(initdir); then \
		$(mkinstalldirs) $(initdir); \
		chmod 755 $(initdir); \
	fi
	if test '!' -f $(initdir)/Default; then \
		$(INSTALL_SCRIPT) $(srcdir)/Default $(initdir)/Default; \
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
