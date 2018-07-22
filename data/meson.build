subdir('applications')
subdir('autostart')
subdir('dconf')
subdir('pixmaps')

# XPath configs
foreach file : [ 'Init', 'PreSession', 'PostSession' ]
  configure_file(
    input: '@0@.in'.format(file),
    output: file,
    configuration: { 'X_PATH':  x_path },
  )
endforeach

# gdm.conf
configure_file(
  output: 'gdm.conf',
  input: 'gdm.conf.in',
  configuration: { 'GDM_USERNAME': get_option('user') },
  install_dir: dbus_sys_dir,
)
configure_file(
  output: 'gdm.conf-custom',
  input: 'gdm.conf-custom.in',
  configuration: { 'GDM_DEFAULT_CONF': gdm_defaults_conf },
  install_mode: 'rw-r--r--',
  install_dir: gdmconfdir,
)

# GSettings schema
install_data('org.gnome.login-screen.gschema.xml',
  install_dir: gdm_datadir / 'glib-2.0' / 'schemas',
)
compiled_gschema = gnome.compile_schemas()

# gdm.schema
gdm_schema_confdata = configuration_data()
gdm_schema_confdata.set('GDMPREFETCHCMD', 'GDMPREFETCHCMD')
gdm_schema_confdata.set('GDM_CUSTOM_CONF', 'GDM_CUSTOM_CONF')
gdm_schema_confdata.set('GDM_USER_PATH', 'GDM_USER_PATH')
gdm_schema_confdata.set('GDM_USERNAME', 'GDM_USERNAME')
gdm_schema_confdata.set('GDM_GROUPNAME', 'GDM_GROUPNAME')
gdm_schema_confdata.set('HALT_COMMAND', 'HALT_COMMAND')
gdm_schema_confdata.set('REBOOT_COMMAND', 'REBOOT_COMMAND')
gdm_schema_confdata.set('SOUND_PROGRAM', 'SOUND_PROGRAM')
gdm_schema_confdata.set('SUSPEND_COMMAND', 'SUSPEND_COMMAND')
gdm_schema_confdata.set('XEVIE_OPTION', 'XEVIE_OPTION')
gdm_schema_confdata.set('ALWAYS_RESTART_SERVER', 'ALWAYS_RESTART_SERVER')
gdm_schema_confdata.set('X_CONFIG_OPTIONS', 'X_CONFIG_OPTIONS')
gdm_schema_confdata.set('X_SERVER', 'X_SERVER')
gdm_schema_confdata.set('GDM_RBAC_SYSCMD_KEYS', 'GDM_RBAC_SYSCMD_KEYS')
gdm_schema_confdata.set('datadir', 'datadir')
gdm_schema_confdata.set('gdmconfdir', 'gdmconfdir')
gdm_schema_confdata.set('libdir', 'libdir')
gdm_schema_confdata.set('libexecdir', 'libexecdir')
gdm_schema_confdata.set('gdmlocaledir', 'gdmlocaledir')
gdm_schema_confdata.set('logdir', 'logdir')
gdm_schema_confdata.set('pixmapdir', 'pixmapdir')
gdm_schema_confdata.set('sbindir', 'sbindir')
gdm_schema = configure_file(
  input: 'gdm.schemas.in',
  output: 'gdm.schemas',
  configuration: gdm_schema_confdata,
  install_dir: get_option('datadir') / 'gdm',
)

install_data('locale.alias',
  install_dir: gdm_datadir / 'gdm',
)
configure_file(
  input: 'gnome-login.session.in',
  output: 'gnome-login.session',
  configuration: {
    'libexecdir': gdm_prefix / get_option('libexecdir'),
    'check_accelerated_dir': check_accelerated_dir,
  },
  install_dir: gdm_datadir / 'gnome-session' / 'sessions',
)

gdm_rules = configure_file(
  input: '61-gdm.rules.in',
  output: '61-gdm.rules',
  configuration: {
    'libexecdir': gdm_prefix / get_option('libexecdir'),
  },
  install_dir: udev_dir,
)

# DBus service files
service_config = configuration_data()
# service_config.set('sbindir', sbindir) # XXX
service_config.set('GDM_INITIAL_VT', get_option('initial-vt'))
service_config.set('LANG_CONFIG_FILE', lang_config_file)
if plymouth_dep.found()
  service_config.set('PLYMOUTH_QUIT_SERVICE', 'plymouth-quit.service')
else
  service_config.set('PLYMOUTH_QUIT_SERVICE', '')
endif
# XXX more

configure_file(
  input: 'gdm.service.in',
  output: 'gdm.service',
  configuration: service_config,
  install_dir: gdm_datadir / 'dbus-1' / 'services', # XXX systemd unit
)

# XSession
if install_gdm_xsession
  configure_file(
    input: 'XSession.in',
    output: 'XSession',
    configuration: {
      'libexecdir': gdm_prefix / get_option('libexecdir'),
      'XSESSION_SHELL': gdm_xsession_shell,
    },
    install_dir: gdmconfdir,
  )
endif
