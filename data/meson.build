subdir('applications')
subdir('autostart')
subdir('dconf')

# XPath configs
foreach file : [ 'Init', 'PreSession', 'PostSession' ]
  generated_file = configure_file(
    input: '@0@.in'.format(file),
    output: file,
    configuration: { 'X_PATH':  x_path },
  )

  install_data(generated_file,
    install_dir: gdmconfdir / file,
    install_mode: 'rwxr-xr-x',
    rename: 'Default'
  )
endforeach

install_data('PostLogin',
  rename: 'Default.sample',
  install_mode: 'rwxr-xr-x',
  install_dir: gdmconfdir / 'PostLogin',
)

# gdm.conf
configure_file(
  input: 'gdm.conf.in',
  output: '@BASENAME@',
  configuration: {
    'GDM_USERNAME': get_option('user')
  },
  install_dir: dbus_sys_dir,
)
configure_file(
  input: 'gdm.conf-custom.in',
  output: gdm_custom_conf.split('/')[-1],
  copy: true,
  install_mode: 'rw-r--r--',
  install_dir: run_command(find_program('dirname'), gdm_custom_conf).stdout().strip(),
)

# GSettings schema
install_data('org.gnome.login-screen.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas',
)
compiled_gschema = gnome.compile_schemas()

# gdm.schema
gdm_schema = configure_file(
  input: 'gdm.schemas.in',
  output: '@BASENAME@',
  configuration: {
    'GDM_USERNAME': get_option('user'),
    'GDM_GROUPNAME': get_option('group'),
    'gdmconfdir': gdmconfdir,
  },
  install_dir: get_option('datadir') / 'gdm',
)

install_data('locale.alias',
  install_dir: get_option('datadir') / 'gdm',
)

gdm_gnome_shell_component = 'org.gnome.Shell'
gdm_gnome_session_required_components = [
  'org.gnome.SettingsDaemon.A11ySettings',
  'org.gnome.SettingsDaemon.Color',
  'org.gnome.SettingsDaemon.Datetime',
  'org.gnome.SettingsDaemon.Housekeeping',
  'org.gnome.SettingsDaemon.Keyboard',
  'org.gnome.SettingsDaemon.MediaKeys',
  'org.gnome.SettingsDaemon.Power',
  'org.gnome.SettingsDaemon.PrintNotifications',
  'org.gnome.SettingsDaemon.Rfkill',
  'org.gnome.SettingsDaemon.ScreensaverProxy',
  'org.gnome.SettingsDaemon.Sharing',
  'org.gnome.SettingsDaemon.Smartcard',
  'org.gnome.SettingsDaemon.Sound',
  'org.gnome.SettingsDaemon.Wacom',
]

gdm_gnome_user_session_wanted_components = gdm_gnome_session_required_components
gdm_gnome_user_session_wanted_components += [
  'org.gnome.SettingsDaemon.XSettings',
]

configure_file(
  input: 'gnome-login.session.in',
  output: '@BASENAME@',
  configuration: {
    'libexecdir': gdm_prefix / get_option('libexecdir'),
    'check_accelerated_dir': check_accelerated_dir,
    'gnome_required_components': ';'.join(
      [ gdm_gnome_shell_component ] + gdm_gnome_session_required_components),
  },
  install_dir: get_option('datadir') / 'gnome-session' / 'sessions',
)

# PAM
pam_data_files_map = {
  'redhat': [
    'gdm-autologin',
    'gdm-launch-environment',
    'gdm-fingerprint',
    'gdm-smartcard',
    'gdm-password',
    'gdm-pin',
  ],
  'openembedded': [
    'gdm-autologin',
    'gdm-launch-environment',
    'gdm-password',
    'gdm-pin',
  ],
  'exherbo': [
    'gdm-autologin',
    'gdm-launch-environment',
    'gdm-fingerprint',
    'gdm-smartcard',
    'gdm-password',
    'gdm-pin',
  ],
  'lfs': [
    'gdm-autologin',
    'gdm-launch-environment',
    'gdm-fingerprint',
    'gdm-smartcard',
    'gdm-password',
    'gdm-pin',
  ],
  'arch': [
    'gdm-autologin',
    'gdm-launch-environment',
    'gdm-fingerprint',
    'gdm-smartcard',
    'gdm-password',
    'gdm-pin',
  ],
  'none': [],
  # We should no longer have 'autodetect' at this point
}

pam_data_files = pam_data_files_map[default_pam_config]
pam_prefix = (get_option('pam-prefix') != '')? get_option('pam-prefix') : get_option('sysconfdir')
foreach _pam_filename : pam_data_files
  install_data('pam-@0@/@1@.pam'.format(default_pam_config, _pam_filename),
    rename: _pam_filename,
    install_dir: pam_prefix / 'pam.d',
  )
endforeach

gdm_rules = configure_file(
  input: '61-gdm.rules.in',
  output: '@BASENAME@',
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

if get_option('systemduserunitdir') != ''
  systemd_userunitdir = get_option('systemduserunitdir')
else
  systemd_userunitdir = systemd_dep.get_pkgconfig_variable('systemduserunitdir',
                                                           define_variable: ['prefix', get_option('prefix')])
endif

configure_file(
  input: 'gdm.service.in',
  output: '@BASENAME@',
  configuration: service_config,
  install_dir: systemd_systemunitdir,
  format: 'cmake'
)

gdm_gnome_session_wanted_targets = []
foreach component: gdm_gnome_user_session_wanted_components
  gdm_gnome_session_wanted_targets += 'Wants=@0@.target'.format(component)
endforeach

configure_file(
  input: 'session.conf.in',
  output: 'session.conf',
  configuration: {
    'requires_component': gdm_gnome_shell_component,
    'wants_required_components': '\n'.join(gdm_gnome_session_wanted_targets),
  },
  install_dir: systemd_userunitdir / 'gnome-session@gnome-login.target.d',
)

# XSession
if get_option('gdm-xsession')
  configure_file(
    input: 'Xsession.in',
    output: '@BASENAME@',
    configuration: {
      'libexecdir': gdm_prefix / get_option('libexecdir'),
      'XSESSION_SHELL': get_option('solaris')? '/bin/ksh' : '/bin/sh',
    },
    install_dir: gdmconfdir,
  )
endif
