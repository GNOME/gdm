# FIXME: We can't use gnome.gdbus_codegen(autocleanup: 'all') here since it's a bit too new
libgdm_manager_dbus_gen = custom_target('gdm-manager-glue',
    input: '../daemon/gdm-manager.xml',
    output: [ 'gdm-manager-glue.c', 'gdm-manager-glue.h' ],
    command: [ 'gdbus-codegen',
      '--c-namespace', 'Gdm',
      '--interface-prefix=org.gnome.DisplayManager',
      '--generate-c-code=gdm-manager-glue',
      '--c-generate-autocleanup=all',
      '--annotate', 'org.gnome.DisplayManager.Manager', 'org.gtk.GDBus.C.Name', 'Manager',
      '--output-directory', '@OUTDIR@',
      '@INPUT@'
    ],
    build_by_default: true,
)
libgdm_client_dbus_gen = custom_target('gdm-client-glue',
    input: '../daemon/gdm-session.xml',
    output: [ 'gdm-client-glue.c', 'gdm-client-glue.h' ],
    command: [ 'gdbus-codegen',
      '--c-namespace', 'Gdm',
      '--interface-prefix=org.gnome.DisplayManager',
      '--generate-c-code=gdm-client-glue',
      '--c-generate-autocleanup=all',
      '--annotate', 'org.gnome.DisplayManager.UserVerifier', 'org.gtk.GDBus.C.Name', 'UserVerifier',
      '--annotate', 'org.gnome.DisplayManager.Greeter', 'org.gtk.GDBus.C.Name', 'Greeter',
      '--annotate', 'org.gnome.DisplayManager.RemoteGreeter', 'org.gtk.GDBus.C.Name', 'RemoteGreeter',
      '--annotate', 'org.gnome.DisplayManager.Chooser', 'org.gtk.GDBus.C.Name', 'Chooser',
      '--output-directory', '@OUTDIR@',
      '@INPUT@'
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

libgdm_deps = [
  glib_dep,
  gio_dep,
  gio_unix_dep,
  libsystemd_dep,
]

libgdm = library('gdm',
  libgdm_src,
  dependencies: libgdm_deps,
  include_directories: config_h_dir,
)

libgdm_dep = declare_dependency(
  link_with: libgdm,
  dependencies: libgdm_deps,
  include_directories: include_directories('.'),
)
