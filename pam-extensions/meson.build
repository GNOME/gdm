if pam_extensions_supported
  pkgconf_config = configuration_data()
  pkgconf_config.set_quoted('prefix', gdm_prefix)
  pkgconf_config.set_quoted('libdir', gdm_libdir)
  pkgconf_config.set_quoted('includedir', gdm_includedir)
  pkgconf_config.set_quoted('VERSION', meson.project_version())

  configure_file(
    output: 'gdm-pam-extensions.pc',
    input: 'gdm-pam-extensions.pc.in',
    configuration: pkgconf_config,
    install: true,
    install_dir: join_paths(gdm_libdir, 'pkgconfig'),
  )

  pam_extensions_includedir = include_directories('.')
endif
