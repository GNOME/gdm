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
    install_header: true,
    install_dir: get_option('includedir') / meson.project_name()
)

libgdm_built_sources = [
  libgdm_manager_dbus_gen,
  libgdm_client_dbus_gen,
]

libgdm_sources = files(
  'gdm-client.c',
  'gdm-sessions.c',
  'gdm-user-switching.c',
)

libgdm_public_headers = [
  'gdm-client.h',
  'gdm-sessions.h',
  'gdm-user-switching.h',
  # gdm-client-glue.h is automatically installed
]

install_headers(libgdm_public_headers,
  subdir: meson.project_name(),
)

libgdm_headers = [
  libgdm_public_headers,
  libgdm_client_dbus_gen[1],
  libgdm_manager_dbus_gen[1],
]

libgdm_deps = [
  glib_dep,
  gio_dep,
  gio_unix_dep,
  logind_dep,
  libgdmcommon_dep,
]

libgdm_link_flags = [
  '-Wl,--version-script,@0@/libgdm.map'.format(meson.current_source_dir()),
]

libgdm = library('gdm',
  libgdm_sources + libgdm_built_sources,
  version: '1.0.0',
  dependencies: libgdm_deps,
  include_directories: config_h_dir,
  link_args: libgdm_link_flags,
  install: true,
)

libgdm_gir_includes = [
  'GLib-2.0',
  'GObject-2.0',
  'Gio-2.0',
]

libgdm_dep = declare_dependency(
  link_with: libgdm,
  dependencies: libgdm_deps,
  include_directories: include_directories('.'),
  sources: libgdm_built_sources,
)

libgdm_gir = gnome.generate_gir(libgdm,
  sources: [
    libgdm_headers,
    libgdm_sources,
    libgdm_built_sources,
  ],
  namespace: 'Gdm',
  nsversion: '1.0',
  identifier_prefix: 'Gdm',
  includes: libgdm_gir_includes,
  install: true,
)

# Pkg-config file
pkgconfig.generate(libgdm,
  name: 'GDM Client Library',
  description: 'Client Library for communicating with GDM daemon',
  filebase: meson.project_name(),
  requires: gobject_dep,
)
