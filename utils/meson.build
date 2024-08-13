# gdm-flexiserver
gdm_flexiserver_deps = [
  glib_dep,
  libgdmcommon_dep,
]

gdm_flexiserver = executable('gdmflexiserver',
  'gdmflexiserver.c',
  dependencies: gdm_flexiserver_deps,
  include_directories: config_h_dir,
  install: true,
)

# gdm-runtime-config
gdm_runtime_config_deps = [
  glib_dep,
]

gdm_runtime_config = executable('gdm-runtime-config',
  'gdm-runtime-config.c',
  dependencies: gdm_runtime_config_deps,
  include_directories: config_h_dir,
  install: true,
  install_dir: get_option('libexecdir'),
)

gdm_auth_config = executable('gdm-config',
  'gdm-config.c',
  dependencies: [
    glib_dep,
    gio_dep,
    libgdmcommon_dep,
  ],
  include_directories: config_h_dir,
  install: true,
)

auth_config_distro_hooks = [
  'redhat',
  'generic',
]

if distro != 'none'
  if distro not in auth_config_distro_hooks and pam_data_files_map.has_key(distro)
    distro = 'generic'
  endif

  gdm_distro_hook = install_data('gdm-auth-config-@0@'.format(distro),
    install_mode: 'rwxr-xr-x',
    install_dir: get_option('libexecdir'),
  )
endif
