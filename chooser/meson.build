# Simple chooser
gdm_simple_chooser_src = [
 'chooser-main.c',
 'gdm-chooser-host.c',
 'gdm-chooser-session.c',
 'gdm-host-chooser-dialog.c',
 'gdm-host-chooser-widget.c',
]

gdm_simple_chooser_deps = [
  glib_dep,
  gtk_dep,
  libgdmcommon_dep,
  libgdm_dep,
  x_deps,
  xdmcp_dep,
]

gdm_simple_chooser = executable('gdm-simple-chooser',
  gdm_simple_chooser_src,
  dependencies: gdm_simple_chooser_deps,
  include_directories: config_h_dir,
  install: true,
  install_dir: get_option('libexecdir'),
)

# Host chooser
gdm_host_chooser_src = [
 'gdm-host-chooser.c',
 'gdm-chooser-host.c',
 'gdm-host-chooser-dialog.c',
 'gdm-host-chooser-widget.c',
]

gdm_host_chooser_deps = [
  glib_dep,
  gtk_dep,
  libgdmcommon_dep,
  libgdm_dep,
  x_deps,
  xdmcp_dep,
]

gdm_host_chooser = executable('gdm-host-chooser',
  gdm_host_chooser_src,
  dependencies: gdm_host_chooser_deps,
  include_directories: config_h_dir,
  install: true,
  install_dir: get_option('libexecdir'),
)
