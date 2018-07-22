libgdm_manager_dbus_gen = gnome.gdbus_codegen('gdm-manager-glue',
    sources: '..' / 'daemon' / 'gdm-manager.xml',
    namespace: 'Gdm',
    interface_prefix: 'org.gnome.DisplayManager',
    autocleanup: 'all',
    annotations: [
      [ 'org.gnome.DisplayManager.Manager', 'org.gtk.GDBus.C.Name', 'Manager' ],
    ],
    build_by_default: true,
)
libgdm_client_dbus_gen = gnome.gdbus_codegen('gdm-client-glue',
    sources: '..' / 'daemon' / 'gdm-session.xml',
    namespace: 'Gdm',
    interface_prefix: 'org.gnome.DisplayManager',
    autocleanup: 'all',
    annotations: [
      [ 'org.gnome.DisplayManager.UserVerifier', 'org.gtk.GDBus.C.Name', 'UserVerifier' ],
      [ 'org.gnome.DisplayManager.Greeter', 'org.gtk.GDBus.C.Name', 'Greeter' ],
      [ 'org.gnome.DisplayManager.RemoteGreeter', 'org.gtk.GDBus.C.Name', 'RemoteGreeter' ],
      [ 'org.gnome.DisplayManager.Chooser', 'org.gtk.GDBus.C.Name', 'Chooser' ],
    ],
    build_by_default: true,
)

libgdm_src = [
  'gdm-client.c',
  'gdm-sessions.c',
  'gdm-user-switching.c',
  libgdm_manager_dbus_gen,
  libgdm_client_dbus_gen,
]

libgdm_headers = [
  'gdm-client.h',
  'gdm-sessions.h',
  'gdm-user-switching.h',
  libgdm_client_dbus_gen[1],
  libgdm_manager_dbus_gen[1],
]

libgdm_deps = [
  glib_dep,
  gio_dep,
  gio_unix_dep,
  libsystemd_dep,
  libgdmcommon_dep,
]

libgdm = shared_library('gdm',
  libgdm_src,
  version: '1.0.0',
  dependencies: libgdm_deps,
  include_directories: config_h_dir,
  install: true,
)

libgdm_gir_includes = [
  'GLib-2.0',
  'GObject-2.0',
  'Gio-2.0',
]

libgdm_gir = gnome.generate_gir(libgdm,
  sources: libgdm_headers,
  namespace: 'Gdm',
  nsversion: '1.0',
  identifier_prefix: 'Gdm',
  includes: libgdm_gir_includes,
  install: true,
)

libgdm_dep = declare_dependency(
  link_with: libgdm,
  dependencies: libgdm_deps,
  include_directories: include_directories('.'),
)

# Pkg-config file
pkgconfig.generate(libgdm,
  name: 'GDM Client Library',
  description: 'Client Library for communicating with GDM daemon',
  filebase: meson.project_name(),
  requires: gobject_dep,
  variables: [
    # NOTE: Meson does not provide an exec_prefix option, is it necessary for GDM though?
    'exec_prefix=' + get_option('prefix'),
  ],
)
