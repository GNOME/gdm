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

# gdm-screenshot
gdm_screenshot_deps = [
  glib_dep,
  gtk_dep,
  x_deps,
  libcanberra_gtk_dep,
]

gdm_screenshot = executable('gdm-screenshot',
  'gdm-screenshot.c',
  dependencies: gdm_screenshot_deps,
  include_directories: config_h_dir,
  install: true,
)

# gdm-disable-wayland
gdm_disable_wayland_deps = [
  glib_dep,
]

gdm_disable_wayland = executable('gdm-disable-wayland',
  'gdm-disable-wayland.c',
  dependencies: gdm_disable_wayland_deps,
  include_directories: config_h_dir,
  install: true,
  install_dir: get_option('libexecdir'),
)

