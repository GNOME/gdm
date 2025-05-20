# D-Bus interfaces
dbus_gen = gnome.gdbus_codegen('gdm-dbus-glue',
  'org.freedesktop.DBus.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.freedesktop.DBus',
  autocleanup: 'all',
)
display_dbus_gen = gnome.gdbus_codegen('gdm-display-glue',
  'gdm-display.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
  autocleanup: 'all',
)
local_display_dbus_gen = gnome.gdbus_codegen('gdm-local-display-glue',
  'gdm-local-display.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
  autocleanup: 'all',
)
local_display_factory_dbus_gen = gnome.gdbus_codegen('gdm-local-display-factory-glue',
  'gdm-local-display-factory.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
  autocleanup: 'all',
)
remote_display_dbus_gen = gnome.gdbus_codegen('gdm-remote-display-glue',
  'gdm-remote-display.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
  autocleanup: 'all',
)
remote_display_factory_dbus_gen = gnome.gdbus_codegen('gdm-remote-display-factory-glue',
  'gdm-remote-display-factory.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
  autocleanup: 'all',
)
manager_dbus_gen = gnome.gdbus_codegen('gdm-manager-glue',
  'gdm-manager.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
  autocleanup: 'all',
)
session_dbus_gen = gnome.gdbus_codegen('gdm-session-glue',
  'gdm-session.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
  autocleanup: 'all',
)
session_worker_dbus_gen = gnome.gdbus_codegen('gdm-session-worker-glue',
  'gdm-session-worker.xml',
  namespace: 'GdmDBus',
  interface_prefix: 'org.gnome.DisplayManager',
  autocleanup: 'all',
)

gdm_session_enums = gnome.mkenums('gdm-session-enum-types',
  h_template: 'gdm-session-enum-types.h.in',
  c_template: 'gdm-session-enum-types.c.in',
  sources: 'gdm-session.h',
)
gdm_session_worker_enums = gnome.mkenums('gdm-session-worker-enum-types',
  h_template: 'gdm-session-worker-enum-types.h.in',
  c_template: 'gdm-session-worker-enum-types.c.in',
  sources: 'gdm-session-worker.h',
)

# Daemons deps
gdm_daemon_deps = [
  libgdmcommon_dep,
  accountsservice_dep,
  gobject_dep,
  gio_dep,
  gio_unix_dep,
  json_glib,
  libpam_dep,
]
if have_x11_support
  gdm_daemon_deps += [
    xcb_dep,
    x_deps,
  ]
endif

if have_xdmcp and get_option('tcp-wrappers')
  gdm_daemon_deps += libwrap_dep
endif

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
  dbus_gen,
  session_dbus_gen,
  session_worker_dbus_gen,
  gdm_session_enums,
  gdm_session_worker_enums,
]

gdm_session_worker_deps = [
  gdm_daemon_deps,
]

gdm_session_worker_includes = [
  config_h_dir,
]

if pam_extensions_supported
  gdm_session_worker_src += '../pam-extensions/gdm-pam-extensions.h'
  gdm_session_worker_includes += pam_extensions_inc
endif

if libaudit_dep.found()
  gdm_session_worker_deps += libaudit_dep

  gdm_session_worker_src += [
    'gdm-session-linux-auditor.c',
  ]
endif

if have_adt
  gdm_session_worker_deps += adt_dep
  gdm_session_worker_src += 'gdm-session-solaris-auditor.c'
endif

gdm_session_worker = executable('gdm-session-worker',
  gdm_session_worker_src,
  dependencies: gdm_session_worker_deps,
  include_directories: gdm_session_worker_includes,
  install: true,
  install_dir: get_option('libexecdir'),
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
  install_dir: get_option('libexecdir'),
)

# X session
if have_x11_support
  gdm_x_session_src = [
    'gdm-x-session.c',
    manager_dbus_gen,
  ]

  gdm_x_session = executable('gdm-x-session',
    gdm_x_session_src,
    dependencies: gdm_daemon_deps,
    include_directories: gdm_session_worker_includes,
    install: true,
    install_dir: get_option('libexecdir'),
  )
endif

# GDM daemon
gdm_daemon_sources = files(
  'gdm-dbus-util.c',
  'gdm-display-access-file.c',
  'gdm-display-factory.c',
  'gdm-display-store.c',
  'gdm-display.c',
  'gdm-dynamic-user-store.c',
  'gdm-launch-environment.c',
  'gdm-legacy-display.c',
  'gdm-local-display-factory.c',
  'gdm-local-display.c',
  'gdm-remote-display.c',
  'gdm-remote-display-factory.c',
  'gdm-manager.c',
  'gdm-server.c',
  'gdm-session-record.c',
  'gdm-session-worker-common.c',
  'gdm-session-worker-job.c',
  'gdm-session.c',
  'main.c',
)

gdm_daemon_gen_sources = [
  display_dbus_gen,
  local_display_factory_dbus_gen,
  manager_dbus_gen,
  local_display_dbus_gen,
  remote_display_dbus_gen,
  remote_display_factory_dbus_gen,
  session_dbus_gen,
  session_worker_dbus_gen,
  gdm_session_enums,
]

if have_xdmcp
  gdm_daemon_deps += xdmcp_dep

  gdm_daemon_sources = [
    gdm_daemon_sources,
    files(
      'gdm-xdmcp-display-factory.c',
      'gdm-xdmcp-display.c',
      'gdm-xdmcp-chooser-display.c',
    ),
  ]
endif

if gudev_dep.found()
  gdm_daemon_deps += gudev_dep
endif

gdm_daemon = executable('gdm',
  [ gdm_daemon_sources, gdm_daemon_gen_sources ],
  dependencies: gdm_daemon_deps,
  include_directories: config_h_dir,
  install: true,
  install_dir: get_option('sbindir')
)
