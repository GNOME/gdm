if pam_extensions_supported
  pkgconfig.generate(
    name: 'GDM PAM Extensions',
    description: 'Macros for custom protocols over PAM',
    filebase: 'gdm-pam-extensions',
    version: meson.project_version(),
    subdirs: 'gdm',
    variables: [
      # NOTE: Meson does not provide an exec_prefix option, is it necessary for GDM though?
      'exec_prefix=' + get_option('prefix'),
    ],
  )

  pam_extensions_includedir = include_directories('.')
endif
