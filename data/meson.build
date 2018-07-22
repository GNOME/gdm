subdir('applications')
subdir('autostart')
subdir('dconf')

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
  copy: true,
  install_mode: 'rw-r--r--',
  install_dir: gdmconfdir,
)

# GSettings schema
install_data('org.gnome.login-screen.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas',
)
compiled_gschema = gnome.compile_schemas()

# gdm.schema
gdm_schema_conf = {
  'GDM_USERNAME': get_option('user'),
  'GDM_GROUPNAME': get_option('group'),
  'gdmconfdir': gdmconfdir,
}
gdm_schema = configure_file(
  input: 'gdm.schemas.in',
  output: 'gdm.schemas',
  configuration: gdm_schema_conf,
  install_dir: get_option('datadir') / 'gdm',
)

install_data('locale.alias',
  install_dir: get_option('datadir') / 'gdm',
)
configure_file(
  input: 'gnome-login.session.in',
  output: 'gnome-login.session',
  configuration: {
    'libexecdir': gdm_prefix / get_option('libexecdir'),
    'check_accelerated_dir': check_accelerated_dir,
  },
  install_dir: get_option('datadir') / 'gnome-session' / 'sessions',
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
service_config.set('sbindir', gdm_prefix / get_option('sbindir'))
service_config.set('GDM_INITIAL_VT', get_option('initial-vt'))
service_config.set('LANG_CONFIG_FILE', lang_config_file)
if plymouth_dep.found()
  service_config.set('PLYMOUTH_QUIT_SERVICE', 'plymouth-quit.service')
else
  service_config.set('PLYMOUTH_QUIT_SERVICE', '')
endif

if get_option('systemdsystemunitdir') != ''
  systemd_systemunitdir = get_option('systemdsystemunitdir')
else
  systemd_systemunitdir = systemd_dep.get_pkgconfig_variable('systemdsystemunitdir')
endif

configure_file(
  input: 'gdm.service.in',
  output: 'gdm.service',
  configuration: service_config,
  install_dir: systemd_systemunitdir,
)

# XSession
if get_option('gdm-xsession')
  xsession_shell = get_option('solaris')? '/bin/ksh' : '/bin/sh'
  configure_file(
    input: 'XSession.in',
    output: 'XSession',
    configuration: {
      'libexecdir': gdm_prefix / get_option('libexecdir'),
      'XSESSION_SHELL': xsession_shell,
    },
    install_dir: gdmconfdir,
  )
endif
