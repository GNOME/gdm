pixmapdir = $(datadir)/pixmaps
confdir = $(gdmconfdir)
localedir = $(gdmconfdir)
bisessdir = $(datadir)/gdm/BuiltInSessions
instsessdir = $(datadir)/xsessions
initdir = $(gdmconfdir)/Init
authdir = $(localstatedir)/gdm
logdir = $(localstatedir)/log/gdm
gnomercdir = $(gdmconfdir)
postdir = $(gdmconfdir)/PostSession
predir = $(gdmconfdir)/PreSession
postlogindir = $(gdmconfdir)/PostLogin

noinst_DATA = gdm.conf

DESKTOP_FILES = default.desktop CDE.desktop gnome.desktop @SSHDESKTOP@

EXTRA_DIST = \
	gdm.conf.in \
	gdm.conf-custom \
	$(DESKTOP_FILES) \
	default.desktop.in \
	gnome.desktop.in \
	CDE.desktop.in \
	ssh.desktop.in.in \
	Xsession.in \
	gdm \
	gdm-autologin \
	locale.alias \
	Init.in \
	PreSession.in \
	PostSession.in \
	PostLogin \
	XKeepsCrashing \
	gettextfoo.h \
	gdmprefetchlist.in \
	extract-shell.sh

CLEANFILES = Xsession gdm.conf default.desktop gnome.desktop CDE.desktop ssh.desktop ssh.desktop.in Init PreSession PostSession gdmprefetchlist

Xsession: $(srcdir)/Xsession.in
	sed	-e 's,[@]XSESSION_SHELL[@],$(XSESSION_SHELL),g' \
		-e 's,[@]libexecdir[@],$(libexecdir),g' \
		<$(srcdir)/Xsession.in >Xsession

gdm.conf: $(srcdir)/gdm.conf.in
	sed	-e 's,[@]GDMPREFETCHCMD[@],$(GDMPREFETCHCMD),g' \
		-e 's,[@]GDM_USER_PATH[@],$(GDM_USER_PATH),g' \
		-e 's,[@]HALT_COMMAND[@],$(HALT_COMMAND),g' \
		-e 's,[@]REBOOT_COMMAND[@],$(REBOOT_COMMAND),g' \
		-e 's,[@]SOUND_PROGRAM[@],$(SOUND_PROGRAM),g' \
		-e 's,[@]SUSPEND_COMMAND[@],$(SUSPEND_COMMAND),g' \
		-e 's,[@]XEVIE_OPTION[@],$(XEVIE_OPTION),g' \
		-e 's,[@]X_CONFIG_OPTIONS[@],$(X_CONFIG_OPTIONS),g' \
		-e 's,[@]X_SERVER[@],$(X_SERVER),g' \
		-e 's,[@]X_XNEST_CONFIG_OPTIONS[@],$(X_XNEST_CONFIG_OPTIONS),g' \
		-e 's,[@]X_XNEST_PATH[@],$(X_XNEST_PATH),g' \
		-e 's,[@]authdir[@],$(authdir),g' \
		-e 's,[@]datadir[@],$(datadir),g' \
		-e 's,[@]dmconfdir[@],$(dmconfdir),g' \
		-e 's,[@]gdmconfdir[@],$(gdmconfdir),g' \
		-e 's,[@]libdir[@],$(libdir),g' \
		-e 's,[@]libexecdir[@],$(libexecdir),g' \
		-e 's,[@]localedir[@],$(libexecdir),g' \
		-e 's,[@]logdir[@],$(logdir),g' \
		-e 's,[@]pixmapdir[@],$(pixmapdir),g' \
		-e 's,[@]sbindir[@],$(sbindir),g' \
		<$(srcdir)/gdm.conf.in >gdm.conf

gettextfoo.h: XKeepsCrashing Xsession.in
	cat $^ | $(srcdir)/extract-shell.sh > gettextfoo.h

@INTLTOOL_DESKTOP_RULE@

clean-local:
	rm -f $(DESKTOP_FILES)

uninstall-hook:
	rm -f $(DESTDIR)$(bisessdir)/gnome.desktop \
	$(DESTDIR)$(bisessdir)/default.desktop \
	$(DESTDIR)$(bisessdir)/CDE.desktop \
	$(DESTDIR)$(instsessdir)/gnome.desktop \
	$(DESTDIR)$(instsessdir)/default.desktop \
	$(DESTDIR)$(instsessdir)/ssh.desktop \
	$(DESTDIR)$(instsessdir)/CDE.desktop \
	$(DESTDIR)$(GDM_DEFAULTS_CONF) \
	$(DESTDIR)$(GDM_CUSTOM_CONF) \
	`dirname $(DESTDIR)$(GDM_DEFAULTS_CONF)`/factory-`basename $(DESTDIR)$(GDM_DEFAULTS_CONF)` \
	$(DESTDIR)$(confdir)/XKeepsCrashing \
	$(DESTDIR)$(confdir)/Xsession \
	$(DESTDIR)$(confdir)/gdmprefetchlist \
	$(DESTDIR)$(localedir)/locale.alias \
	$(DESTDIR)$(initdir)/Default \
	$(DESTDIR)$(postlogindir)/Default.sample \
	$(DESTDIR)$(predir)/Default \
	$(DESTDIR)$(postdir)/Default

install-data-hook: gdm.conf gdm.conf-custom Xsession Init PostSession PreSession $(DESKTOP_FILES) $(GDMPREFETCHLIST)
	if test '!' -d $(DESTDIR)$(confdir); then \
		$(mkinstalldirs) $(DESTDIR)$(confdir); \
		chmod 755 $(DESTDIR)$(confdir); \
	fi
	if test '!' -d $(DESTDIR)$(dmconfdir); then \
		$(mkinstalldirs) $(DESTDIR)$(dmconfdir); \
		chmod 755 $(DESTDIR)$(dmconfdir); \
	fi
	if test '!' -d `dirname $(DESTDIR)$(GDM_DEFAULTS_CONF)`; then \
		$(mkinstalldirs) `dirname $(DESTDIR)$(GDM_DEFAULTS_CONF)`; \
		chmod 755 `dirname $(DESTDIR)$(GDM_DEFAULTS_CONF)`; \
	fi
	if test '!' -d `dirname $(DESTDIR)$(GDM_CUSTOM_CONF)`; then \
		$(mkinstalldirs) `dirname $(DESTDIR)$(GDM_CUSTOM_CONF)`; \
		chmod 755 `dirname $(DESTDIR)$(GDM_CUSTOM_CONF)`; \
	fi
	if test -f $(DESTDIR)$(confdir)/gdm.conf; then \
	   if ! cmp -s $(DESTDIR)$(confdir)/gdm.conf $(DESTDIR)$(confdir)/factory-gdm.conf > /dev/null 2>&1 ; then \
		if test '!' -f $(DESTDIR)$(GDM_CUSTOM_CONF); then \
			mv -f $(DESTDIR)$(confdir)/gdm.conf $(DESTDIR)$(GDM_CUSTOM_CONF); \
		else \
			mv -f $(DESTDIR)$(confdir)/gdm.conf $(DESTDIR)$(GDM_DEFAULTS_CONF).org; \
		fi; \
	   fi; \
	fi
	$(INSTALL_DATA) gdm.conf $(DESTDIR)$(GDM_DEFAULTS_CONF)
	chmod 444 $(DESTDIR)$(GDM_DEFAULTS_CONF)
	if test '!' -f $(DESTDIR)$(GDM_CUSTOM_CONF); then \
		$(INSTALL_DATA) $(srcdir)/gdm.conf-custom $(DESTDIR)$(GDM_CUSTOM_CONF); \
		chmod 644 $(DESTDIR)$(GDM_CUSTOM_CONF); \
	fi
	$(INSTALL_DATA) gdm.conf `dirname $(DESTDIR)$(GDM_DEFAULTS_CONF)`/factory-`basename $(DESTDIR)$(GDM_DEFAULTS_CONF)`

	$(INSTALL_SCRIPT) $(srcdir)/XKeepsCrashing $(DESTDIR)$(confdir)/XKeepsCrashing
	$(INSTALL_SCRIPT) Xsession $(DESTDIR)$(confdir)/Xsession

	-if test -f $(DESTDIR)$(localedir)/locale.alias; then \
		cp -f $(DESTDIR)$(localedir)/locale.alias $(DESTDIR)$(localedir)/locale.alias.orig; \
	fi
	$(INSTALL_DATA) $(srcdir)/locale.alias $(DESTDIR)$(localedir)/locale.alias

	if test '!' -d $(DESTDIR)$(bisessdir); then \
		$(mkinstalldirs) $(DESTDIR)$(bisessdir); \
		chmod 755 $(DESTDIR)$(bisessdir); \
	fi

	if test '!' -d $(DESTDIR)$(instsessdir); then \
		$(mkinstalldirs) $(DESTDIR)$(instsessdir); \
		chmod 755 $(DESTDIR)$(instsessdir); \
	fi

	-if test -f $(DESTDIR)$(bisessdir)/Default.desktop; then \
		mv -f $(DESTDIR)$(bisessdir)/Default.desktop $(DESTDIR)$(bisessdir)/Default.desktop.orig; \
	fi
	-if test -f $(DESTDIR)$(bisessdir)/default.desktop; then \
		cp -f $(DESTDIR)$(bisessdir)/default.desktop $(DESTDIR)$(bisessdir)/default.desktop.orig; \
	fi
	$(INSTALL_DATA) default.desktop $(DESTDIR)$(bisessdir)/default.desktop
	chmod 644 $(DESTDIR)$(bisessdir)/default.desktop

	-if test -f $(DESTDIR)$(bisessdir)/gnome.desktop; then \
		mv -f $(DESTDIR)$(bisessdir)/gnome.desktop $(DESTDIR)$(bisessdir)/gnome.desktop.orig; \
	fi
	-if test -f $(DESTDIR)$(instsessdir)/gnome.desktop; then \
		cp -f $(DESTDIR)$(instsessdir)/gnome.desktop $(DESTDIR)$(instsessdir)/gnome.desktop.orig; \
	fi
	$(INSTALL_DATA) gnome.desktop $(DESTDIR)$(instsessdir)/gnome.desktop
	chmod 644 $(DESTDIR)$(instsessdir)/gnome.desktop

	-if test -f /usr/dt/bin/Xsession; then \
	   if test -f $(DESTDIR)$(bisessdir)/CDE.desktop; then \
		mv -f $(DESTDIR)$(bisessdir)/CDE.desktop $(DESTDIR)$(bisessdir)/CDE.desktop.orig; \
	   fi; \
	   if test -f $(DESTDIR)$(instsessdir)/CDE.desktop; then \
		cp -f $(DESTDIR)$(instsessdir)/CDE.desktop $(DESTDIR)$(instsessdir)/CDE.desktop.orig; \
	   fi; \
	   $(INSTALL_DATA) CDE.desktop $(DESTDIR)$(instsessdir)/CDE.desktop; \
	fi

	if test '!' -d $(DESTDIR)$(initdir); then \
		$(mkinstalldirs) $(DESTDIR)$(initdir); \
		chmod 755 $(DESTDIR)$(initdir); \
	fi
	-if test -f $(DESTDIR)$(initdir)/Default; then \
		cp -f $(DESTDIR)$(initdir)/Default $(DESTDIR)$(initdir)/Default.orig; \
	fi
	$(INSTALL_SCRIPT) Init $(DESTDIR)$(initdir)/Default

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
		chmod 755 $(DESTDIR)$(logdir); \
		chown root:root $(DESTDIR)$(logdir); \
	fi

	if test '!' -d $(DESTDIR)$(authdir); then \
		$(mkinstalldirs) $(DESTDIR)$(authdir); \
		chmod 1770 $(DESTDIR)$(authdir); \
		chown root:gdm $(DESTDIR)$(authdir); \
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

	if test "x$(GDMPREFETCHLIST)" != "x"; then \
	   $(INSTALL_DATA) gdmprefetchlist $(DESTDIR)$(confdir)/gdmprefetchlist; \
	fi

	if test "x$(SSHDESKTOP)" != "x"; then \
	   if test -f $(DESTDIR)$(instsessdir)/ssh.desktop; then \
		cp -f $(DESTDIR)$(instsessdir)/ssh.desktop $(DESTDIR)$(instsessdir)/ssh.desktop.orig; \
	   fi; \
	   $(INSTALL_SCRIPT) ssh.desktop $(DESTDIR)$(instsessdir)/ssh.desktop; \
	   chmod 644 $(DESTDIR)$(instsessdir)/ssh.desktop; \
	fi

