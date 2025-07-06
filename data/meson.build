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
    'GDM_USERNAME': get_option('user'),
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
  install_dir: gdm_datadir,
)

install_data('locale.alias',
  install_dir: gdm_datadir,
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
  ],
  'openembedded': [
    'gdm-autologin',
    'gdm-launch-environment',
    'gdm-password',
  ],
  'exherbo': [
    'gdm-autologin',
    'gdm-launch-environment',
    'gdm-fingerprint',
    'gdm-smartcard',
    'gdm-password',
  ],
  'lfs': [
    'gdm-autologin',
    'gdm-launch-environment',
    'gdm-fingerprint',
    'gdm-smartcard',
    'gdm-password',
  ],
  'arch': [
    'gdm-autologin',
    'gdm-launch-environment',
    'gdm-fingerprint',
    'gdm-smartcard',
    'gdm-password',
  ],
}

pam_data_files = pam_data_files_map.get(default_pam_config, [])
foreach _pam_filename : pam_data_files
  install_data('pam-@0@/@1@.pam'.format(default_pam_config, _pam_filename),
    rename: _pam_filename,
    install_dir: pam_profiles_dir,
  )
endforeach

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

systemdsystemunitdir = get_option('systemdsystemunitdir')
if systemdsystemunitdir != 'no'
  if get_option('systemdsystemunitdir') != ''
    systemd_systemunitdir = get_option('systemdsystemunitdir')
  else
    assert(systemd_dep.found(), 'systemd required but not found, please provide a valid systemd system unit dir or disable it')
    systemd_systemunitdir = systemd_dep.get_variable(pkgconfig: 'systemdsystemunitdir')
  endif
endif

systemduserunitdir = get_option('systemduserunitdir')
if systemduserunitdir != 'no'
  if get_option('systemduserunitdir') != ''
    systemd_userunitdir = get_option('systemduserunitdir')
  else
    assert(systemd_dep.found(), 'systemd required but not found, please provide a valid systemd user unit dir or disable it')
    systemd_userunitdir = systemd_dep.get_variable(pkgconfig: 'systemduserunitdir',
                                                   pkgconfig_define: ['prefix', get_option('prefix')])
  endif
endif

if systemdsystemunitdir != 'no'
  configure_file(
    input: 'gdm.service.in',
    output: '@BASENAME@',
    configuration: service_config,
    install_dir: systemd_systemunitdir,
    format: 'cmake'
  )
endif

gdm_gnome_session_wanted_targets = []
foreach component: gdm_gnome_user_session_wanted_components
  gdm_gnome_session_wanted_targets += 'Wants=@0@.target'.format(component)
endforeach

if systemduserunitdir != 'no'
  configure_file(
    input: 'session.conf.in',
    output: 'session.conf',
    configuration: {
      'requires_component': gdm_gnome_shell_component,
      'wants_required_components': '\n'.join(gdm_gnome_session_wanted_targets),
    },
    install_dir: systemd_userunitdir / 'gnome-session@gnome-login.target.d',
  )
endif

# XSession
if get_option('gdm-xsession') and have_x11_support
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

# Polkit
configure_file(
  input: 'polkit-gdm.rules.in',
  output: '20-gdm.rules',
  configuration: {
    'GDM_USERNAME': get_option('user'),
  },
  install_dir: get_option('datadir') / 'polkit-1' / 'rules.d',
)
