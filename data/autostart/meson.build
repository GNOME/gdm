autostart_files_conf = configuration_data()
autostart_files_conf.set('LIBEXECDIR', gdm_libexecdir)

foreach autostart_file : [ 'orca-autostart.desktop' ]
  configure_file(
    input: autostart_file,
    output: autostart_file,
    configuration: autostart_files_conf,
    install_dir: gdm_datadir / 'gdm' / 'greeter' / 'autostart',
  )
endforeach
