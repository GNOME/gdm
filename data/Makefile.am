NULL =

dbusconfdir = $(DBUS_SYS_DIR)
dbusconf_DATA = gdm.conf

@INTLTOOL_SCHEMAS_RULE@

schemasdir = $(gdmconfdir)
schemas_in_files = gdm.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

gdm.schemas.in: $(srcdir)/gdm.schemas.in.in
	sed	-e 's,[@]GDMPREFETCHCMD[@],$(GDMPREFETCHCMD),g' \
		-e 's,[@]GDM_CUSTOM_CONF[@],$(GDM_CUSTOM_CONF),g' \
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
		<$(srcdir)/gdm.schemas.in.in >gdm.schemas.in

EXTRA_DIST =						\
	$(schemas_DATA)					\
	$(dbusconf_DATA)				\
	$(NULL)

CLEANFILES = 				\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in
