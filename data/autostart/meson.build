autostart_files_conf = {
  'LIBEXECDIR': gdm_prefix / get_option('libexecdir'),
}

foreach autostart_file : [ 'orca-autostart.desktop' ]
  configure_file(
    input: autostart_file,
    output: autostart_file,
    configuration: autostart_files_conf,
    install_dir: get_option('datadir') / 'gdm' / 'greeter' / 'autostart',
  )
endforeach
