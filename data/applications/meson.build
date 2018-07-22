desktop_conf = configuration_data()
# desktop_conf.set('SBINDIR', gdm_sbindir) XXX
desktop_conf.set('SYSCONFDIR', gdm_sysconfdir)
desktop_conf.set('LIBDIR', gdm_libdir)
desktop_conf.set('LIBEXECDIR', gdm_libexecdir)
desktop_conf.set('LOCALSTATEDIR', gdm_localstatedir)

foreach desktop_file : [ 'mime-dummy-handler.desktop', 'mimeapps.list' ]
  configure_file(
    input: desktop_file,
    output: desktop_file,
    configuration: desktop_conf,
    install: true,
    install_dir: join_paths(gdm_datadir, 'gdm', 'greeter', 'applications'),
  )
endforeach
