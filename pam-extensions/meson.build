if pam_extensions_supported
  pkgconfig.generate(
    name: 'GDM PAM Extensions',
    description: 'Macros for custom protocols over PAM',
    filebase: 'gdm-pam-extensions',
    version: meson.project_version(),
    subdirs: meson.project_name(),
  )

  header_files = files('gdm-pam-extensions.h',
                       'gdm-pam-extensions-common.h',
                       'gdm-choice-list-pam-extension.h',
                       'gdm-custom-json-pam-extension.h')

  pam_extensions_inc = include_directories('.')
  install_headers(header_files,
    subdir: meson.project_name()
  )
endif
