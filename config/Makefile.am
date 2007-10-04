NULL =

pixmapdir = $(datadir)/pixmaps
confdir = $(gdmconfdir)
gdmlocaledir = $(gdmconfdir)
bisessdir = $(datadir)/gdm/BuiltInSessions
instsessdir = $(datadir)/xsessions
initdir = $(gdmconfdir)/Init
authdir = $(localstatedir)/gdm
gnomercdir = $(gdmconfdir)
postdir = $(gdmconfdir)/PostSession
predir = $(gdmconfdir)/PreSession
postlogindir = $(gdmconfdir)/PostLogin
# logdir is set by configure.ac

noinst_DATA = \
	gdm.conf		\
	$(NULL)

EXTRA_DIST = 			\
	gdm.conf.in 		\
	gdm.conf-custom.in 	\
	Xsession.in 		\
	gdm 			\
	gdm-autologin 		\
	Init.in 		\
	PreSession.in 		\
	PostSession.in 		\
	PostLogin 		\
	$(NULL)

CLEANFILES = 			\
	Xsession		\
	gdm.conf		\
	gdm.conf-custom		\
	Init			\
	PreSession		\
	PostSession		\
	$(NULL)

Xsession: $(srcdir)/Xsession.in
	sed	-e 's,[@]XSESSION_SHELL[@],$(XSESSION_SHELL),g' \
		-e 's,[@]libexecdir[@],$(libexecdir),g' \
		<$(srcdir)/Xsession.in >Xsession

gdm.conf: $(srcdir)/gdm.conf.in
	sed     -e 's,[@]GDM_CUSTOM_CONF[@],$(GDM_CUSTOM_CONF),g' \
		-e 's,[@]GDM_USER_PATH[@],$(GDM_USER_PATH),g' \
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
		-e 's,[@]dmconfdir[@],$(dmconfdir),g' \
		-e 's,[@]gdmconfdir[@],$(gdmconfdir),g' \
		-e 's,[@]libdir[@],$(libdir),g' \
		-e 's,[@]libexecdir[@],$(libexecdir),g' \
		-e 's,[@]gdmlocaledir[@],$(gdmlocaledir),g' \
		-e 's,[@]logdir[@],$(logdir),g' \
		-e 's,[@]pixmapdir[@],$(pixmapdir),g' \
		-e 's,[@]sbindir[@],$(sbindir),g' \
		<$(srcdir)/gdm.conf.in >gdm.conf

gdm.conf-custom: $(srcdir)/gdm.conf-custom.in
	sed	-e 's,[@]GDM_DEFAULTS_CONF[@],$(GDM_DEFAULTS_CONF),g' \
		<$(srcdir)/gdm.conf-custom.in >gdm.conf-custom

@INTLTOOL_DESKTOP_RULE@

uninstall-hook:
	rm -f \
	$(DESTDIR)$(GDM_DEFAULTS_CONF) \
	$(DESTDIR)$(GDM_CUSTOM_CONF) \
	`dirname $(DESTDIR)$(GDM_DEFAULTS_CONF)`/factory-`basename $(DESTDIR)$(GDM_DEFAULTS_CONF)` \
	$(DESTDIR)$(confdir)/Xsession \
	$(DESTDIR)$(initdir)/Default \
	$(DESTDIR)$(postlogindir)/Default.sample \
	$(DESTDIR)$(predir)/Default \
	$(DESTDIR)$(postdir)/Default

install-data-hook: gdm.conf gdm.conf-custom Xsession Init PostSession PreSession
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
		$(INSTALL_DATA) gdm.conf-custom $(DESTDIR)$(GDM_CUSTOM_CONF); \
		chmod 644 $(DESTDIR)$(GDM_CUSTOM_CONF); \
	fi
	$(INSTALL_DATA) gdm.conf `dirname $(DESTDIR)$(GDM_DEFAULTS_CONF)`/factory-`basename $(DESTDIR)$(GDM_DEFAULTS_CONF)`

	$(INSTALL_SCRIPT) Xsession $(DESTDIR)$(confdir)/Xsession

	if test '!' -d $(DESTDIR)$(bisessdir); then \
		$(mkinstalldirs) $(DESTDIR)$(bisessdir); \
		chmod 755 $(DESTDIR)$(bisessdir); \
	fi

	if test '!' -d $(DESTDIR)$(instsessdir); then \
		$(mkinstalldirs) $(DESTDIR)$(instsessdir); \
		chmod 755 $(DESTDIR)$(instsessdir); \
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
		$(INSTALL_DATA) gdm $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm; \
	   fi; \
	   if test $$system = Linux && test '!' -f $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-autologin; then \
		$(INSTALL_DATA) gdm-autologin $(DESTDIR)$(PAM_PREFIX)/pam.d/gdm-autologin; \
	   fi; \
	   if test $$system = SunOS; then \
		echo "Please add PAM authentication for gdm and gdm-autologin in $(PAM_PREFIX)/pam.conf!"; \
	   fi; \
	fi

