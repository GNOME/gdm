libgdmcommon_src = files(
  'gdm-address.c',
  'gdm-common.c',
  'gdm-file-utils.c',
  'gdm-log.c',
  'gdm-profile.c',
  'gdm-settings-backend.c',
  'gdm-settings-desktop-backend.c',
  'gdm-settings-direct.c',
  'gdm-settings-utils.c',
  'gdm-settings.c',
)

libgdmcommon_deps = [
  logind_dep,
  gobject_dep,
  gio_dep,
  gio_unix_dep,
]

if libselinux_dep.found()
  libgdmcommon_deps += libselinux_dep
endif

libgdmcommon_lib = static_library('gdmcommon',
  libgdmcommon_src,
  dependencies: libgdmcommon_deps,
  include_directories: config_h_dir,
)

libgdmcommon_dep = declare_dependency(
  link_with: libgdmcommon_lib,
  dependencies: libgdmcommon_deps,
  include_directories: include_directories('.'),
)

install_data('gdb-cmd')

# test-log exectuable
test_log = executable('test-log',
  'test-log.c',
  dependencies: libgdmcommon_dep,
  include_directories: config_h_dir,
)
