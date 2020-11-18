desktop_conf = {
  'LIBDIR': gdm_prefix / get_option('libdir'),
  'LIBEXECDIR': gdm_prefix / get_option('libexecdir'),
  'LOCALSTATEDIR': gdm_prefix / get_option('localstatedir'),
  'SBINDIR': gdm_prefix / get_option('sbindir'),
  'SYSCONFDIR': gdm_prefix / get_option('sysconfdir'),
}

foreach desktop_file : [ 'mime-dummy-handler.desktop', 'mimeapps.list' ]
  configure_file(
    input: desktop_file,
    output: desktop_file,
    configuration: desktop_conf,
    install_dir: gdm_datadir / 'greeter' / 'applications',
  )
endforeach
