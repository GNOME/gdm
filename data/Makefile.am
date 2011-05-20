NULL =

SUBDIRS =			\
	applications		\
	pixmaps			\
	$(NULL)

initdir = $(gdmconfdir)/Init
authdir = $(localstatedir)/gdm
postdir = $(gdmconfdir)/PostSession
predir = $(gdmconfdir)/PreSession
postlogindir = $(gdmconfdir)/PostLogin
workingdir = $(GDM_WORKING_DIR)
xauthdir = $(GDM_XAUTH_DIR)
screenshotdir = $(GDM_SCREENSHOT_DIR)
cachedir = $(localstatedir)/cache/gdm

Xsession: $(srcdir)/Xsession.in
	sed	-e 's,[@]XSESSION_SHELL[@],$(XSESSION_SHELL),g' \
		-e 's,[@]libexecdir[@],$(libexecdir),g' \
		<$(srcdir)/Xsession.in >Xsession

Init: $(srcdir)/Init.in
	sed	-e 's,[@]X_PATH[@],$(X_PATH),g' \
		<$(srcdir)/Init.in >Init

PreSession: $(srcdir)/PreSession.in
	sed	-e 's,[@]X_PATH[@],$(X_PATH),g' \
		<$(srcdir)/PreSession.in >PreSession
PostSession: $(srcdir)/PostSession.in
	sed	-e 's,[@]X_PATH[@],$(X_PATH),g' \
		<$(srcdir)/PostSession.in >PostSession

gdm.conf-custom: $(srcdir)/gdm.conf-custom.in
	sed	-e 's,[@]GDM_DEFAULTS_CONF[@],$(GDM_DEFAULTS_CONF),g' \
		<$(srcdir)/gdm.conf-custom.in >gdm.conf-custom

dbusconfdir = $(DBUS_SYS_DIR)
dbusconf_in_files = gdm.conf.in
dbusconf_DATA = $(dbusconf_in_files:.conf.in=.conf)

@INTLTOOL_SCHEMAS_RULE@

schemasdir = $(pkgdatadir)
schemas_in_files = gdm.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

gdm.schemas.in: $(srcdir)/gdm.schemas.in.in
	sed	-e 's,[@]GDMPREFETCHCMD[@],$(GDMPREFETCHCMD),g' \
		-e 's,[@]GDM_CUSTOM_CONF[@],$(GDM_CUSTOM_CONF),g' \
		-e 's,[@]GDM_USER_PATH[@],$(GDM_USER_PATH),g' \
		-e 's,[@]GDM_USERNAME[@],$(GDM_USERNAME),g' \
		-e 's,[@]GDM_GROUPNAME[@],$(GDM_GROUPNAME),g' \
		-e 's,[@]HALT_COMMAND[@],$(HALT_COMMAND),g' \
		-e 's,[@]REBOOT_COMMAND[@],$(REBOOT_COMMAND),g' \
		-e 's,[@]SOUND_PROGRAM[@],$(SOUND_PROGRAM),g' \
		-e 's,[@]SUSPEND_COMMAND[@],$(SUSPEND_COMMAND),g' \
		-e 's,[@]XEVIE_OPTION[@],$(XEVIE_OPTION),g' \
		-e 's,[@]ALWAYS_RESTART_SERVER[@],$(ALWAYS_RESTART_SERVER),g' \
		-e 's,[@]X_CONFIG_OPTIONS[@],$(X_CONFIG_OPTIONS),g' \
		-e 's,[@]X_SERVER[@],$(X_SERVER),g' \
		-e 's,[@]X_XNEST_CMD[@],$(X_XNEST_CMD),g' \
		-e 's,[@]X_XNEST_CONFIG_OPTIONS[@],$(X_XNEST_CONFIG_OPTIONS),g' \
		-e 's,[@]X_XNEST_UNSCALED_FONTPATH[@],$(X_XNEST_UNSCALED_FONTPATH),g' \
		-e 's,[@]GDM_RBAC_SYSCMD_KEYS[@],$(GDM_RBAC_SYSCMD_KEYS),g' \
		-e 's,[@]authdir[@],$(authdir),g' \
		-e 's,[@]datadir[@],$(datadir),g' \
		-e 's,[@]gdmconfdir[@],$(gdmconfdir),g' \
		-e 's,[@]libdir[@],$(libdir),g' \
		-e 's,[@]libexecdir[@],$(libexecdir),g' \
		-e 's,[@]gdmlocaledir[@],$(gdmlocaledir),g' \
		-e 's,[@]logdir[@],$(logdir),g' \
		-e 's,[@]pixmapdir[@],$(pixmapdir),g' \
		-e 's,[@]sbindir[@],$(sbindir),g' \
		<$(srcdir)/gdm.schemas.in.in >gdm.schemas.in

gdm-shell.session: $(srcdir)/gdm-shell.session.in
	sed	-e 's,[@]libexecdir[@],$(libexecdir),g' \
		< $(srcdir)/gdm-shell.session.in > gdm-shell.session

localealiasdir = $(datadir)/gdm
localealias_DATA = locale.alias

sessiondir = $(datadir)/gnome-session/sessions
session_DATA = gdm-fallback.session gdm-shell.session gdm-setup.session

policydir = $(datadir)/gdm
policy_DATA = 20-gdm-initial-setup.pkla

EXTRA_DIST =			\
	$(schemas_in_files)	\
	$(schemas_DATA)		\
	$(dbusconf_in_files)	\
	$(localealias_DATA)	\
	$(policy_DATA)		\
	gdm.schemas.in.in	\
	gdm.conf-custom.in 	\
	Xsession.in 		\
	gdm 			\
	gdm-autologin 		\
	gdm-welcome 		\
	gdm-fallback.session	\
	Init.in 		\
	PreSession.in 		\
	PostSession.in 		\
	PostLogin 		\
	gconf.path		\
	session-setup.entries	\
	gdm-shell.session.in    \
	make-dconf-override-db.sh \
	dconf-profile \
	$(NULL)

CLEANFILES = 				\
	Xsession			\
	gdm.conf-custom			\
	Init				\
	PreSession			\
	PostSession			\
	dconf-override-db		\
	$(NULL)

DISTCLEANFILES =			\
	$(dbusconf_DATA)		\
	gdm-shell.session		\
	gdm.schemas			\
	dconf-override-db		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	dconf-override-db		\
	Makefile.in

dconf-override-db: $(srcdir)/make-dconf-override-db.sh
	$(srcdir)/make-dconf-override-db.sh

dist-hook: $(srcdir)/dconf-override-db
	cp $(srcdir)/dconf-override-db $(distdir)

uninstall-hook:
	rm -f \
	$(DESTDIR)$(GDM_CUSTOM_CONF) \
	$(DESTDIR)$(gdmconfdir)/Xsession \
	$(DESTDIR)$(initdir)/Default \
	$(DESTDIR)$(postlogindir)/Default.sample \
	$(DESTDIR)$(predir)/Default \
	$(DESTDIR)$(postdir)/Default \
	$(DESTDIR)$(PAM_PREFIX)/pam.d/gdm \
	$(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-autologin \
	$(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-welcome \
	$(DESTDIR)$(workingdir)/.gconf.path \
	$(DESTDIR)$(sysconfdir)/dconf/db/gdm \
	$(DESTDIR)$(sysconfdir)/dconf/profile/gdm \
	-rf \
	$(DESTDIR)$(workingdir)/.gconf.mandatory \
	$(DESTDIR)$(workingdir)/.config/dconf \
	$(DESTDIR)$(screenshotdir) \
	$(DESTDIR)$(xauthdir)

	-rmdir \
	$(DESTDIR)$(sysconfdir)/dconf/db \
	$(DESTDIR)$(sysconfdir)/dconf

install-data-hook: gdm.conf-custom Xsession Init PostSession PreSession gconf.path dconf-override-db
	if test '!' -d $(DESTDIR)$(gdmconfdir); then \
		$(mkinstalldirs) $(DESTDIR)$(gdmconfdir); \
		chmod 755 $(DESTDIR)$(gdmconfdir); \
	fi
	if test '!' -f $(DESTDIR)$(GDM_CUSTOM_CONF); then \
		$(INSTALL_DATA) gdm.conf-custom $(DESTDIR)$(GDM_CUSTOM_CONF); \
		chmod 644 $(DESTDIR)$(GDM_CUSTOM_CONF); \
	fi

	$(INSTALL_SCRIPT) Xsession $(DESTDIR)$(gdmconfdir)/Xsession

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

	if test '!' -d $(DESTDIR)$(logdir); then \
		$(mkinstalldirs) $(DESTDIR)$(logdir); \
		chmod 755 $(DESTDIR)$(logdir); \
		chown root:root $(DESTDIR)$(logdir) || : ; \
	fi

	if test '!' -d $(DESTDIR)$(authdir); then \
		$(mkinstalldirs) $(DESTDIR)$(authdir); \
		chmod 1770 $(DESTDIR)$(authdir); \
		chown root:gdm $(DESTDIR)$(authdir) || : ; \
	fi

	system=`uname`; \
	if test -f /usr/include/security/pam_appl.h; then \
	  if test '!' -d $(DESTDIR)$(PAM_PREFIX)/pam.d; then \
		$(mkinstalldirs) $(DESTDIR)$(PAM_PREFIX)/pam.d; \
		chmod 755 $(DESTDIR)$(PAM_PREFIX)/pam.d; \
	   fi; \
	   if test $$system = Linux && test '!' -f $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm; then \
		$(INSTALL_DATA) $(srcdir)/gdm $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm; \
	   fi; \
	   if test $$system = Linux && test '!' -f $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-autologin; then \
		$(INSTALL_DATA) $(srcdir)/gdm-autologin $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-autologin; \
	   fi; \
	   if test $$system = Linux && test '!' -f $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-welcome; then \
		$(INSTALL_DATA) $(srcdir)/gdm-welcome $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-welcome; \
	   fi; \
	   if test $$system = SunOS; then \
		echo "Please add PAM authentication for gdm, gdm-autologin and gdm-welcome in $(PAM_PREFIX)/pam.conf!"; \
	   fi; \
	fi

	if test '!' -d $(DESTDIR)$(xauthdir); then \
		$(mkinstalldirs) $(DESTDIR)$(xauthdir); \
		chmod 0711 $(DESTDIR)$(xauthdir); \
		chown root:gdm $(DESTDIR)$(xauthdir) || : ; \
	fi

	if test '!' -d $(DESTDIR)$(screenshotdir); then \
		$(mkinstalldirs) $(DESTDIR)$(screenshotdir); \
		chmod 0755 $(DESTDIR)$(screenshotdir); \
		chown gdm:gdm $(DESTDIR)$(screenshotdir) || : ; \
	fi

	if test '!' -d $(DESTDIR)$(workingdir); then \
		$(mkinstalldirs) $(DESTDIR)$(workingdir); \
		chmod 1770 $(DESTDIR)$(workingdir); \
		chown root:gdm $(DESTDIR)$(workingdir) || : ; \
	fi

	if test '!' -d $(DESTDIR)$(workingdir)/.config/dconf; then \
		$(mkinstalldirs) $(DESTDIR)$(workingdir)/.config/dconf; \
		chmod 0755 $(DESTDIR)$(workingdir)/.config/dconf; \
		chown gdm:gdm $(DESTDIR)$(workingdir)/.config/dconf || : ; \
	fi

	if test '!' -d $(DESTDIR)$(workingdir)/.local/share/applications; then \
		$(mkinstalldirs) $(DESTDIR)$(workingdir)/.local/share/applications; \
		chmod 0755 $(DESTDIR)$(workingdir)/.local/share/applications; \
		chown gdm:gdm $(DESTDIR)$(workingdir)/.local/share/applications || : ; \
	fi

	if test '!' -d $(DESTDIR)$(sysconfdir)/dconf/profile; then \
		$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/dconf/profile; \
		chmod 0755 $(DESTDIR)$(sysconfdir)/dconf/profile; \
		chown root:root $(DESTDIR)$(sysconfdir)/dconf/profile || : ; \
	fi

	if test '!' -d $(DESTDIR)$(sysconfdir)/dconf/db; then \
		$(mkinstalldirs) $(DESTDIR)$(sysconfdir)/dconf/db; \
		chmod 0755 $(DESTDIR)$(sysconfdir)/dconf/db; \
		chown root:root $(DESTDIR)$(sysconfdir)/dconf/db || : ; \
	fi

	if test '!' -d $(DESTDIR)$(cachedir); then \
		$(mkinstalldirs) $(DESTDIR)$(cachedir); \
		chmod 1755 $(DESTDIR)$(cachedir); \
		chown root:gdm $(DESTDIR)$(cachedir) || : ; \
	fi

	$(INSTALL_DATA) $(srcdir)/gconf.path $(DESTDIR)$(workingdir)/.gconf.path
	gconftool-2 --direct --config-source=xml:merged:$(DESTDIR)$(workingdir)/.gconf.mandatory --recursive-unset /
	gconftool-2 --direct --config-source=xml:merged:$(DESTDIR)$(workingdir)/.gconf.mandatory --load $(srcdir)/session-setup.entries
	chown -R root:gdm $(DESTDIR)$(workingdir)/.gconf.mandatory || :
	chmod 1750 $(DESTDIR)$(workingdir)/.gconf.mandatory
	chmod 1640 $(DESTDIR)$(workingdir)/.gconf.mandatory/*.xml

	$(INSTALL_DATA) $(srcdir)/dconf-override-db $(DESTDIR)$(sysconfdir)/dconf/db/gdm
	$(INSTALL_DATA) $(srcdir)/dconf-profile $(DESTDIR)$(sysconfdir)/dconf/profile/gdm
