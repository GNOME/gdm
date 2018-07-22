
gdm_dconf_conf = configuration_data()
gdm_dconf_conf.set('DATADIR', gdm_datadir) # XXX correct?
gdm_dconf_conf.set('PACKAGE', meson.project_name()) # XXX correct?
configure_file(
  input: 'gdm.in',
  output: 'gdm',
  configuration: gdm_dconf_conf,
  install_dir: gdm_datadir / 'dconf' / 'profile',
)
