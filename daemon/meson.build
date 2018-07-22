# D-Bus interfaces
display_dbus_gen = gnome.gdbus_codegen('gdm-display-glue',
  'gdm-display.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
)
local_display_dbus_gen = gnome.gdbus_codegen('gdm-local-display-glue',
  'gdm-local-display.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
)
local_display_factory_dbus_gen = gnome.gdbus_codegen('gdm-local-display-factory-glue',
  'gdm-local-display-factory.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
)
manager_dbus_gen = gnome.gdbus_codegen('gdm-manager-glue',
  'gdm-manager.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
)
session_dbus_gen = gnome.gdbus_codegen('gdm-session-glue',
  'gdm-session.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
)
session_worker_dbus_gen = gnome.gdbus_codegen('gdm-session-worker-glue',
  'gdm-session-worker.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
)

gdm_session_enums = gnome.mkenums('gdm-session-enum-types',
  h_template: 'gdm-session-enum-types.h.in',
  c_template: 'gdm-session-enum-types.c.in',
  sources: 'gdm-session.h',
)

# Daemons deps
gdm_daemon_deps = [
  libgdmcommon_dep,
  accountsservice_dep,
  gobject_dep,
  gio_dep,
  gio_unix_dep,
  libpam_dep,
  x_deps,
  xcb_dep,
]

# test-session-client
test_session_client_src = [
  'test-session-client.c',
  session_dbus_gen,
  manager_dbus_gen,
]

test_session_client = executable('test-session-client',
  test_session_client_src,
  dependencies: gdm_daemon_deps,
  include_directories: config_h_dir,
)

# Session worker
gdm_session_worker_src = [
  'session-worker-main.c',
  'gdm-session.c',
  'gdm-session-settings.c',
  'gdm-session-auditor.c',
  'gdm-session-record.c',
  'gdm-session-worker.c',
  'gdm-session-worker-job.c',
  'gdm-session-worker-common.c',
  'gdm-dbus-util.c',
  session_dbus_gen,
  session_worker_dbus_gen,
  gdm_session_enums,
]

gdm_session_worker_includes = [
  config_h_dir,
]

if pam_extensions_supported
  gdm_session_worker_src += '../pam-extensions/gdm-pam-extensions.h'
  gdm_session_worker_includes += pam_extensions_includedir
endif

# TODO
# if libaudit
# endif

# TODO
# if ADT
# endif

gdm_session_worker = executable('gdm-session-worker',
  gdm_session_worker_src,
  dependencies: gdm_daemon_deps,
  include_directories: gdm_session_worker_includes,
  install: true,
  install_dir: gdm_libexecdir,
)

# Wayland session
gdm_wayland_session_src = [
  'gdm-wayland-session.c',
  manager_dbus_gen,
]

gdm_wayland_session = executable('gdm-wayland-session',
  gdm_wayland_session_src,
  dependencies: gdm_daemon_deps,
  include_directories: gdm_session_worker_includes,
  install: true,
  install_dir: gdm_libexecdir,
)

# X session
# gdm_x_session_src = [
#   'gdm-x-session.c',
#   manager_dbus_gen,
# ]
# 
# gdm_x_session = executable('gdm-x-session',
#   gdm_x_session_src,
#   dependencies: gdm_daemon_deps,
#   include_directories: gdm_session_worker_includes,
#   install: true,
#   install_dir: gdm_libexecdir,
# )

# GDM daemon
gdm_daemon_src = [
  'gdm-dbus-util.c',
  'gdm-display-access-file.c',
  'gdm-display-factory.c',
  'gdm-display-store.c',
  'gdm-display.c',
  'gdm-launch-environment.c',
  'gdm-legacy-display.c',
  'gdm-local-display-factory.c',
  'gdm-local-display.c',
  'gdm-manager.c',
  'gdm-server.c',
  'gdm-session-record.c',
  'gdm-session-worker-common.c',
  'gdm-session-worker-job.c',
  'gdm-session.c',
  'main.c',
  display_dbus_gen,
  local_display_factory_dbus_gen,
  manager_dbus_gen,
  local_display_dbus_gen,
  session_dbus_gen,
  session_worker_dbus_gen,
  gdm_session_enums,
]

gdm_daemon = executable('gdm',
  gdm_daemon_src,
  dependencies: gdm_daemon_deps,
  include_directories: config_h_dir,
  install: true,
  # install_dir: sbindir
)
