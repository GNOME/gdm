gdm_dconf_conf = {
  'DATADIR': gdm_datadir, # XXX correct?
  'PACKAGE': meson.project_name(), # XXX correct?
}

gdm_dconf = configure_file(
  input: 'gdm.in',
  output: 'gdm',
  configuration: gdm_dconf_conf,
  install_dir: get_option('datadir') / 'dconf' / 'profile',
)
