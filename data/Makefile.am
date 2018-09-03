NULL =
EXTRA_DIST =

SUBDIRS =			\
	applications		\
	autostart		\
	pixmaps			\
	$(NULL)

initdir = $(gdmconfdir)/Init
postdir = $(gdmconfdir)/PostSession
predir = $(gdmconfdir)/PreSession
postlogindir = $(gdmconfdir)/PostLogin
workingdir = $(GDM_WORKING_DIR)
xauthdir = $(GDM_XAUTH_DIR)
screenshotdir = $(GDM_SCREENSHOT_DIR)
cachedir = $(localstatedir)/cache/gdm

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

# dconf database and profile
dconf_db_files = \
	dconf/defaults/00-upstream-settings		\
	dconf/defaults/locks/00-upstream-settings-locks

dconfdbdir = $(pkgdatadir)
dconfdb_DATA = greeter-dconf-defaults
greeter-dconf-defaults: $(dconf_db_files)
	$(AM_V_GEN) dconf compile $@ $(srcdir)/dconf/defaults

dconfprofiledir = $(DATADIR)/dconf/profile
dconfprofile_DATA = dconf/gdm

gsettings_SCHEMAS = org.gnome.login-screen.gschema.xml
@GSETTINGS_RULES@

schemasdir = $(pkgdatadir)
schemas_in_files = gdm.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)
$(schemas_DATA): $(schemas_in_files)
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
		-e 's,[@]GDM_RBAC_SYSCMD_KEYS[@],$(GDM_RBAC_SYSCMD_KEYS),g' \
		-e 's,[@]datadir[@],$(datadir),g' \
		-e 's,[@]gdmconfdir[@],$(gdmconfdir),g' \
		-e 's,[@]libdir[@],$(libdir),g' \
		-e 's,[@]libexecdir[@],$(libexecdir),g' \
		-e 's,[@]gdmlocaledir[@],$(gdmlocaledir),g' \
		-e 's,[@]logdir[@],$(logdir),g' \
		-e 's,[@]pixmapdir[@],$(pixmapdir),g' \
		-e 's,[@]sbindir[@],$(sbindir),g' \
		<$(srcdir)/gdm.schemas.in >gdm.schemas


EXTRA_DIST += gnome-login.session.in

gnome-login.session: $(srcdir)/gnome-login.session.in
	sed	-e 's,[@]libexecdir[@],$(libexecdir),g' \
		-e 's,[@]CHECK_ACCELERATED_DIR[@],$(CHECK_ACCELERATED_DIR),g' \
		< $< > $@.tmp && mv $@.tmp $@

localealiasdir = $(datadir)/gdm
localealias_DATA = locale.alias

sessiondir = $(datadir)/gnome-session/sessions
session_DATA =  gnome-login.session

pam_redhat_files = \
	pam-redhat/gdm-autologin.pam		\
	pam-redhat/gdm-launch-environment.pam	\
	pam-redhat/gdm-fingerprint.pam		\
	pam-redhat/gdm-smartcard.pam		\
	pam-redhat/gdm-password.pam		\
	pam-redhat/gdm-pin.pam			\
	$(NULL)
EXTRA_DIST += $(pam_redhat_files)

pam_openembedded_files = pam-openembedded/gdm-password.pam	\
	pam-openembedded/gdm-autologin.pam		\
	pam-openembedded/gdm-launch-environment.pam	\
	pam-openembedded/gdm-pin.pam			\
	$(NULL)
EXTRA_DIST += $(pam_openembedded_files)

pam_exherbo_files = pam-exherbo/gdm-autologin.pam \
	pam-exherbo/gdm-launch-environment.pam	 \
	pam-exherbo/gdm-fingerprint.pam	         \
	pam-exherbo/gdm-smartcard.pam            \
	pam-exherbo/gdm-password.pam             \
	pam-exherbo/gdm-pin.pam	                 \
	$(NULL)
EXTRA_DIST += $(pam_exherbo_files)

pam_lfs_files = \
	pam-lfs/gdm-autologin.pam		\
	pam-lfs/gdm-launch-environment.pam	\
	pam-lfs/gdm-fingerprint.pam		\
	pam-lfs/gdm-smartcard.pam		\
	pam-lfs/gdm-password.pam		\
	pam-lfs/gdm-pin.pam			\
	$(NULL)
EXTRA_DIST += $(pam_lfs_files)

pam_arch_files = pam-arch/gdm-autologin.pam	\
	pam-arch/gdm-launch-environment.pam	\
	pam-arch/gdm-fingerprint.pam		\
	pam-arch/gdm-smartcard.pam		\
	pam-arch/gdm-password.pam		\
	pam-arch/gdm-pin.pam			\
	$(NULL)
EXTRA_DIST += $(pam_arch_files)

if ENABLE_REDHAT_PAM_CONFIG
pam_files = $(pam_redhat_files)
endif
if ENABLE_OPENEMBEDDED_PAM_CONFIG
pam_files = $(pam_openembedded_files)
endif
if ENABLE_EXHERBO_PAM_CONFIG
pam_files = $(pam_exherbo_files)
endif
if ENABLE_LFS_PAM_CONFIG
pam_files = $(pam_lfs_files)
endif
if ENABLE_ARCH_PAM_CONFIG
pam_files = $(pam_arch_files)
endif

if INSTALL_UDEV_RULES
udevrulesdir = $(UDEV_DIR)/rules.d
udevrules_DATA = 61-gdm.rules
endif

EXTRA_DIST += $(srcdir)/61-gdm.rules.in
61-gdm.rules: $(srcdir)/61-gdm.rules.in
	sed	-e 's,[@]libexecdir[@],$(libexecdir),g' \
		< $< > $@.tmp && mv $@.tmp $@

EXTRA_DIST +=			\
	$(dconf_db_files)	\
	$(gsettings_SCHEMAS)	\
	$(schemas_in_files)	\
	$(schemas_DATA)		\
	$(dbusconf_in_files)	\
	$(localealias_DATA)	\
	$(udevrules_DATA)	\
	gdm.conf-custom.in 	\
	Xsession.in 		\
	Init.in 		\
	PreSession.in 		\
	PostSession.in 		\
	PostLogin 		\
	$(NULL)

CLEANFILES = 				\
	Xsession			\
	gdm.conf-custom			\
	Init				\
	PreSession			\
	PostSession			\
	greeter-dconf-defaults		\
	$(NULL)

DISTCLEANFILES =			\
	$(dbusconf_DATA)		\
	gdm.schemas			\
	gnome-login.session		\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

systemdsystemunit =

gdm.service: $(srcdir)/gdm.service.in
	$(AM_V_GEN)sed \
		-e 's,[@]sbindir[@],$(sbindir),g' \
		-e 's,[@]GDM_INITIAL_VT[@],$(GDM_INITIAL_VT),g' \
		-e 's,[@]LANG_CONFIG_FILE[@],$(LANG_CONFIG_FILE),g' \
		-e 's,[@]PLYMOUTH_QUIT_SERVICE[@],$(PLYMOUTH_QUIT_SERVICE),g' \
		-e 's, *$$,,g' \
		< $< > $@
systemdsystemunit += gdm.service
CLEANFILES += gdm.service

if INSTALL_SYSTEMD_UNITS
systemdsystemunit_DATA=$(systemdsystemunit)
systemdsystemunitdir=$(SYSTEMD_SYSTEM_UNIT_DIR)
endif

Xsession_files =
if ENABLE_GDM_XSESSION

Xsession: $(srcdir)/Xsession.in
	sed	-e 's,[@]XSESSION_SHELL[@],$(XSESSION_SHELL),g' \
		-e 's,[@]libexecdir[@],$(libexecdir),g' \
		<$(srcdir)/Xsession.in >Xsession
Xsession_files += Xsession
CLEANFILES += Xsession

endif

EXTRA_DIST += gdm.service.in

uninstall-hook:
	rm -f \
	$(DESTDIR)$(GDM_CUSTOM_CONF) \
	$(DESTDIR)$(gdmconfdir)/Xsession \
	$(DESTDIR)$(initdir)/Default \
	$(DESTDIR)$(postlogindir)/Default.sample \
	$(DESTDIR)$(predir)/Default \
	$(DESTDIR)$(postdir)/Default \
	$(DESTDIR)$(sysconfdir)/dconf/db/gdm \
	$(DESTDIR)$(sysconfdir)/dconf/profile/gdm \
	-rf \
	$(DESTDIR)$(screenshotdir) \
	$(DESTDIR)$(xauthdir) \
	$(DESTDIR)$(PAM_PREFIX)/pam.d

install-data-hook: gdm.conf-custom $(Xsession_files) Init PostSession PreSession
	if test '!' -d $(DESTDIR)$(gdmconfdir); then \
		$(mkinstalldirs) $(DESTDIR)$(gdmconfdir); \
		chmod 755 $(DESTDIR)$(gdmconfdir); \
	fi
	if test '!' -f $(DESTDIR)$(GDM_CUSTOM_CONF); then \
		$(INSTALL_DATA) gdm.conf-custom $(DESTDIR)$(GDM_CUSTOM_CONF); \
		chmod 644 $(DESTDIR)$(GDM_CUSTOM_CONF); \
	fi

if ENABLE_GDM_XSESSION
	$(INSTALL_SCRIPT) Xsession $(DESTDIR)$(gdmconfdir)/Xsession
endif

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

	system=`uname`; \
	if test -f /usr/include/security/pam_appl.h; then \
	  if test '!' -d $(DESTDIR)$(PAM_PREFIX)/pam.d; then \
		$(mkinstalldirs) $(DESTDIR)$(PAM_PREFIX)/pam.d; \
		chmod 755 $(DESTDIR)$(PAM_PREFIX)/pam.d; \
	   fi; \
	   if test $$system = Linux; then \
	     for pamfile in $(pam_files); do \
	       bn=$$(basename $$pamfile .pam); \
	       $(INSTALL_DATA) $(srcdir)/$$pamfile $(DESTDIR)$(PAM_PREFIX)/pam.d/$$bn; \
	     done; \
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

	if test '!' -d $(DESTDIR)$(workingdir)/.local/share/applications; then \
		$(mkinstalldirs) $(DESTDIR)$(workingdir)/.local/share/applications; \
		chmod 0755 $(DESTDIR)$(workingdir)/.local/share/applications; \
		chown gdm:gdm $(DESTDIR)$(workingdir)/.local/share/applications || : ; \
	fi

	if test '!' -d $(DESTDIR)$(cachedir); then \
		$(mkinstalldirs) $(DESTDIR)$(cachedir); \
		chmod 1755 $(DESTDIR)$(cachedir); \
		chown root:gdm $(DESTDIR)$(cachedir) || : ; \
	fi
