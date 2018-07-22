gdm_dconf = configure_file(
  input: 'gdm.in',
  output: 'gdm',
  configuration: {
    'DATADIR': gdm_prefix / get_option('datadir'),
    'PACKAGE': meson.project_name(),
  },
  install_dir: get_option('datadir') / 'dconf' / 'profile',
)
