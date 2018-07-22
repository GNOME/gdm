subdir('applications')
subdir('autostart')
subdir('pixmaps')

# XPath configs
x_path_conf = configuration_data()
x_path_conf.set('X_PATH', x_path) #XXX quoted?
foreach file : [ 'Init', 'PreSession', 'PostSession' ]
  configure_file(
    input: '@0@.in'.format(file),
    output: file,
    configuration: x_path_conf,
  )
endforeach

subdir('dconf')

compiled_gschema = gnome.compile_schemas()
install_data('org.gnome.login-screen.gschema.xml',
  install_dir: join_paths(gdm_datadir, 'glib-2.0', 'schemas'),
)

install_data('locale.alias',
  install_dir: join_paths(gdm_datadir, 'gdm'),
)
configure_file(
  input: 'gnome-login.session.in',
  output: 'gnome-login.session',
  configuration: configuration_data(),
  install_dir: join_paths(gdm_datadir, 'gnome-session', 'sessions'),
)

rules_conf = configuration_data()
rules_conf.set('libexecdir', gdm_libexecdir)
configure_file(
  input: '61-gdm.rules.in',
  output: '61-gdm.rules',
  install: true,
  configuration: rules_conf,
  install_dir: join_paths(gdm_prefix, 'lib', 'udev', 'rules.d'),
)

# DBus service files
service_config = configuration_data()
# service_config.set('sbindir', join_paths(sbindir)) # XXX
service_config.set('GDM_INITIAL_VT', gdm_initial_vt) # XXX
service_config.set('LANG_CONFIG_FILE', lang_config_file)
service_config.set('PLYMOUTH_QUIT_SERVICE', lang_config_file) # XXX
# XXX more

configure_file(
  input: 'gdm.service.in',
  output: 'gdm.service',
  configuration: service_config,
  install: true,
  # install_dir: join_paths(datadir, 'dbus-1', 'services'), XXX systemd unit
)

# XSession
if install_gdm_xsession
  xsession_config = configuration_data()
#   xsession_config.set('XSESSION_SHELL', xsession_shell) # XXX
  configure_file(
    input: 'XSession.in',
    output: 'XSession',
    install: true,
    install_dir: gdmconfdir,
  )
endif
