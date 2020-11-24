gdm_dconf = configure_file(
  input: 'gdm.in',
  output: '@BASENAME@',
  configuration: {
    'DATADIR': gdm_prefix / get_option('datadir'),
    'PACKAGE': meson.project_name(),
  },
  install_dir: dconf_profiles_path,
)

greeter_dconf_defaults = custom_target('greeter-dconf-defaults',
  output: 'greeter-dconf-defaults',
  input: files(
    'defaults/00-upstream-settings',
    'defaults/locks/00-upstream-settings-locks',
  ),
  command: [
    find_program('dconf'),
    'compile',
    '@OUTPUT@',
    meson.current_source_dir() / 'defaults',
  ],
  install: true,
  install_dir: gdm_datadir,
)
