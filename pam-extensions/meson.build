if pam_extensions_supported
  pkgconfig.generate(
    name: 'GDM PAM Extensions',
    description: 'Macros for custom protocols over PAM',
    filebase: 'gdm-pam-extensions',
    version: meson.project_version(),
    subdirs: meson.project_name(),
  )

  pam_extensions_inc = include_directories('.')
  install_headers('gdm-pam-extensions.h',
    subdir: meson.project_name()
  )
endif
