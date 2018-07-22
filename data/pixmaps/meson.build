gdm_icons = [
  [ 16, [ 'gdm-xnest.png' ] ],
  [ 32, [ 'gdm-setup.png', 'gdm-xnest.png' ] ],
  [ 48, [ 'gdm.png', 'gdm-setup.png', 'gdm-xnest.png' ] ],
]

foreach size : gdm_icons
  size_str = '@0@x@0@'.format(size[0])
  icons_install_dir = gdm_datadir / 'icons' / 'hicolor' / size_str / 'apps'

  foreach png : size[1]
    install_data(size_str / png,
      install_dir: icons_install_dir,
    )
  endforeach
endforeach


install_data(
  'gdm-foot-logo.png',
  'nobody.png',
  'nohost.png',
  install_dir: gdm_datadir / 'pixmaps',
)
