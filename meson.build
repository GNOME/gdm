project('gdm', 'c',
  version: '49.1',
  license: 'GPL2+',
  meson_version: '>= 0.57',
)

# Modules
gnome = import('gnome')
pkgconfig = import('pkgconfig')
i18n = import('i18n')

# Compiler
cc = meson.get_compiler('c')

# Use GNU extensions if available
add_project_arguments('-D_GNU_SOURCE', language: 'c')

# Options
gdm_prefix = get_option('prefix')
gdm_datadir = get_option('datadir') / meson.project_name()

gdmconfdir = (get_option('sysconfsubdir') == '')? gdm_prefix / get_option('sysconfdir') : gdm_prefix / get_option('sysconfdir') / get_option('sysconfsubdir')
dmconfdir = (get_option('dmconfdir') != '')? get_option('dmconfdir') : gdm_prefix / get_option('sysconfdir') / 'dm'
udev_dir = get_option('udev-dir')
at_spi_registryd_dir = (get_option('at-spi-registryd-dir') != '')? get_option('at-spi-registryd-dir') : gdm_prefix / get_option('libexecdir')
lang_config_file = (get_option('lang-file') != '')? get_option('lang-file') : gdm_prefix / get_option('sysconfdir') / 'locale.conf'
pam_mod_dir = (get_option('pam-mod-dir') != '')? get_option('pam-mod-dir') : gdm_prefix / get_option('libdir') / 'security'
dbus_sys_dir = (get_option('dbus-sys') != '')? get_option('dbus-sys') : get_option('sysconfdir') / 'dbus-1' / 'system.d'
gdm_defaults_conf = (get_option('defaults-conf') != '')? get_option('defaults-conf') : gdm_prefix / gdm_datadir / 'defaults.conf'
gdm_custom_conf = (get_option('custom-conf') != '')? get_option('custom-conf') : gdmconfdir / 'custom.conf'
gnome_settings_daemon_dir = (get_option('gnome-settings-daemon-dir') != '')? get_option('gnome-settings-daemon-dir') : gdm_prefix / get_option('libexecdir')
gdm_run_dir = (get_option('run-dir') != '')? get_option('run-dir') : '/run/gdm'
gdm_runtime_conf = (get_option('runtime-conf') != '')? get_option('runtime-conf') : gdm_run_dir / 'custom.conf'
gdm_pid_file = (get_option('pid-file') != '')? get_option('pid-file') : gdm_run_dir / 'gdm.pid'
working_dir = (get_option('working-dir') != '')? get_option('working-dir') : gdm_prefix / get_option('localstatedir') / 'lib' / 'gdm'
gdm_xauth_dir = (get_option('xauth-dir') != '')? get_option('xauth-dir') : gdm_run_dir / 'xauth'
gdm_private_dbus_dir = (get_option('private-dbus-dir') != '')? get_option('private-dbus-dir') : gdm_run_dir / 'dbus'
gdm_dyn_home_dir = (get_option('dyn-home-dir') != '')? get_option('dyn-home-dir') : gdm_run_dir / 'home'
pam_prefix = (get_option('pam-prefix') != '')? get_option('pam-prefix') : get_option('sysconfdir')
pam_profiles_dir = pam_prefix / 'pam.d'
have_x11_support = get_option('x11-support')
# Common variables
config_h_dir = include_directories('.')

if not get_option('wayland-support') and not have_x11_support
  error('GDM requires either Wayland or X11 support')
endif

# Dependencies
udev_dep = dependency('udev')
gudev_dep = dependency('gudev-1.0', version: '>= 232')

# PAM
libpam_dep = cc.find_library('pam')
pam_extensions_supported = cc.has_header_symbol(
  'security/pam_appl.h', 'PAM_BINARY_PROMPT',
  dependencies: libpam_dep)

glib_min_version = '2.68.0'

glib_dep = dependency('glib-2.0', version: '>=' + glib_min_version)
gobject_dep = dependency('gobject-2.0', version: '>=' + glib_min_version)
gio_dep = dependency('gio-2.0', version: '>=' + glib_min_version)
gio_unix_dep = dependency('gio-unix-2.0', version: '>=' + glib_min_version)
dconf_dep = dependency('dconf', required: false)
json_glib = dependency('json-glib-1.0', version: '>= 1.2.0',
  required: pam_extensions_supported)
accountsservice_dep = dependency('accountsservice', version: '>= 0.6.35')
keyutils_dep = dependency('libkeyutils', required: false)
libselinux_dep = dependency('libselinux', required: get_option('selinux'))

# udev
if udev_dir == ''
  if udev_dep.found()
    udev_prefix = udev_dep.get_variable(pkgconfig: 'udevdir')
  else
    udev_prefix = gdm_prefix / 'lib' / 'udev'
  endif
  udev_dir = udev_prefix / 'rules.d'
endif

# X11
# defaults
x_path = '/usr/bin/X11:/usr/X11R6/bin:/opt/X11R6/bin'
x_bin = '/usr/bin/X'
if have_x11_support
  xcb_dep = dependency('xcb')
  x_deps = declare_dependency(
    dependencies: [
      dependency('x11'),
      dependency('xau'),
    ],
  )
  # Xserver 1.17 & later default to -nolisten and require -listen for remote access
  xserver_deps = dependency('xorg-server', version : '>=1.17', required : false)
  xserver_has_listen = xserver_deps.found()
  find_x_server_script = find_program('build-aux/find-x-server.sh', native: true)
  find_x_server_out = run_command(find_x_server_script).stdout().strip()
  if find_x_server_out != ''
    x_bin = find_x_server_out
    x_bin_path_split = x_bin.split('/')
    i = 0
    x_path = '/'
    foreach dir : x_bin_path_split
      if i < x_bin_path_split.length() - 1
        x_path = x_path / dir
      endif
      i = i + 1
    endforeach
  endif

  xdmcp_dep = cc.find_library('Xdmcp', required: get_option('xdmcp'))
  have_xdmcp = xdmcp_dep.found()
  if have_xdmcp
    gtk_dep = dependency('gtk+-3.0', version: '>= 2.91.1')
  endif
  if have_xdmcp and get_option('tcp-wrappers')
    libwrap_dep = cc.find_library('wrap')
  endif
else
  xserver_has_listen = false
  have_xdmcp = false
endif
# systemd
logind_provider = get_option('logind-provider')
systemd_dep = dependency('systemd', required: false)
if logind_provider == 'systemd'
  libsystemd_dep = dependency('libsystemd')
  logind_dep = libsystemd_dep
  systemd_multiseat_x = find_program('systemd-multi-seat-x',
    required: false,
    dirs: [
      systemd_dep.get_variable(pkgconfig: 'systemdutildir'),
      '/lib/systemd',
      '/usr/lib/systemd',
    ])
  systemd_x_server = systemd_multiseat_x.found()? systemd_multiseat_x.path() : '/lib/systemd/systemd-multi-seat-x'

  have_userdb = libsystemd_dep.version().version_compare('>= 257')

  # Our range is documented at https://systemd.io/UIDS-GIDS/
  greeter_uid_min = systemd_dep.get_variable('greeter_uid_min', default_value: '60578')
  greeter_uid_max = systemd_dep.get_variable('greeter_uid_max', default_value: '60705')
else
  elogind_dep = dependency('libelogind')
  logind_dep = elogind_dep
  systemd_x_server = 'disabled'
  have_userdb = false
  greeter_uid_min = 0
  greeter_uid_max = 0
endif

nologin_program = find_program('nologin', required: false)
nologin_path = nologin_program.found()? nologin_program.path() : '/sbin/nologin'

# Plymouth
plymouth_dep = dependency('ply-boot-client', required: get_option('plymouth'))
# Check for Solaris auditing API (ADT)
# Note, Solaris auditing not supported for Solaris 9 or earlier and
# should not be used on these versions of Solaris if auditing is
# required.  Solaris auditing is only supported on systems that
# support the ADT_USER enumeration value.
have_adt = cc.has_header_symbol('bsm/adt.h', 'ADT_USER')
if have_adt
  adt_dep = cc.find_library('bsm')
endif
# libaudit
libaudit_dep = dependency('audit', required: get_option('libaudit'))
# unit testsheadername
libcheck_dep = dependency('check', required: false, version: '>= 0.9.4')

cc.has_header('security/pam_modutil.h')
cc.has_header('security/pam_ext.h')
have_pam_syslog = cc.has_function('pam_syslog', args: '-lpam')

# UTMP
have_utmpx_header = cc.has_header('utmpx.h')
have_utmp_header =  cc.has_header('utmp.h')
utmp_struct = have_utmpx_header? 'struct utmpx' : 'struct utmp'
utmp_include =  have_utmpx_header? '#include <utmpx.h>' : '#include <utmp.h>'
utmp_has_host_field = cc.has_member(utmp_struct, 'ut_host', prefix: utmp_include)
utmp_has_pid_field = cc.has_member(utmp_struct, 'ut_pid', prefix: utmp_include)
utmp_has_id_field = cc.has_member(utmp_struct, 'ut_id', prefix: utmp_include)
utmp_has_name_field = cc.has_member(utmp_struct, 'ut_name', prefix: utmp_include)
utmp_has_type_field = cc.has_member(utmp_struct, 'ut_type', prefix: utmp_include)
utmp_has_exit_e_termination_field = cc.has_member(utmp_struct, 'ut_exit.e_termination', prefix: utmp_include)
utmp_has_user_field = cc.has_member(utmp_struct, 'ut_user', prefix: utmp_include)
utmp_has_time_field = cc.has_member(utmp_struct, 'ut_time', prefix: utmp_include)
utmp_has_tv_field = cc.has_member(utmp_struct, 'ut_tv', prefix: utmp_include)
utmp_has_syslen_field = cc.has_member(utmp_struct, 'ut_syslen', prefix: utmp_include)

# Check for POSIX getpwnam_r
have_posix_getpwnam_r = cc.compiles('''
#define _POSIX_PTHREAD_SEMANTICS
#include <errno.h>
#include <pwd.h>

int main(int argc, char **argv) {
  char buffer[10000];
  struct passwd pwd, *pwptr = &pwd;
  int error;
  errno = 0;
  error = getpwnam_r ("", &pwd, buffer,
                      sizeof (buffer), &pwptr);
   return (error < 0 && errno == ENOSYS)
     || error == ENOSYS;
}
''')

distro_autodetect_map = {
  '/etc/redhat-release': 'redhat',
  '/etc/fedora-release': 'redhat',
  '/etc/exherbo-release': 'exherbo',
  '/etc/arch-release': 'arch',
  '/etc/lfs-release': 'lfs',
}

distro = get_option('distro')
if distro == 'none'
  foreach _file, _pam_conf : distro_autodetect_map
    distro_check = run_command('test', '-f', _file)
    if distro_check.returncode() == 0
      distro = _pam_conf
    endif
  endforeach
endif

# If requested, try autodetecting from release files (see NetworkManager source)
default_pam_config = get_option('default-pam-config')
if default_pam_config == 'autodetect'
  default_pam_config = distro
endif

# dconf paths, we should fix dconf.pc to provide us more real data though
if dconf_dep.found()
  dconf_prefix = dconf_dep.get_variable(pkgconfig: 'prefix')
  dconf_sysconfig = (dconf_prefix == '/usr') ? '/etc' : dconf_prefix + '/etc'
  dconf_datadir = dconf_prefix / 'share'
else
  dconf_sysconfig = gdm_prefix / get_option('sysconfdir')
  dconf_datadir = gdm_prefix / get_option('datadir')
endif

dconf_profiles_path = dconf_datadir / 'dconf' / 'profile'

# Configuration
conf = configuration_data()
conf.set_quoted('G_LOG_DOMAIN', 'Gdm')
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('DATADIR', gdm_prefix / get_option('datadir'))
conf.set_quoted('SYSCONFDIR', gdm_prefix / get_option('sysconfdir'))
conf.set_quoted('BINDIR', gdm_prefix / get_option('bindir'))
conf.set_quoted('LIBDIR', gdm_prefix / get_option('libdir'))
conf.set_quoted('LIBEXECDIR', gdm_prefix / get_option('libexecdir'))
conf.set_quoted('LOGDIR', get_option('log-dir'))
conf.set_quoted('DMCONFDIR', dmconfdir)
conf.set_quoted('DISTRO', distro)
conf.set_quoted('DCONF_SYSCONFIG', dconf_sysconfig)
conf.set_quoted('DCONF_PROFILES_PATH', dconf_profiles_path)
conf.set_quoted('GDMCONFDIR', gdmconfdir)
conf.set_quoted('GDM_DATADIR', gdm_prefix / gdm_datadir)
conf.set_quoted('GDM_XAUTH_DIR', gdm_xauth_dir)
conf.set_quoted('GDM_PRIVATE_DBUS_DIR', gdm_private_dbus_dir)
conf.set_quoted('GDM_RUN_DIR', gdm_run_dir)
conf.set_quoted('GDM_DCONF_PROFILE', 'gdm')
conf.set_quoted('GDM_WORKING_DIR', working_dir)
conf.set_quoted('GNOMELOCALEDIR', gdm_prefix / get_option('localedir'))
conf.set_quoted('AT_SPI_REGISTRYD_DIR', at_spi_registryd_dir)
conf.set_quoted('GDM_PID_FILE', gdm_pid_file)
conf.set_quoted('GNOME_SETTINGS_DAEMON_DIR', gnome_settings_daemon_dir)
conf.set_quoted('LANG_CONFIG_FILE', lang_config_file)
conf.set_quoted('PAM_PROFILES_DIR', pam_profiles_dir[0] == '/' ? pam_profiles_dir : '/' + pam_profiles_dir)
conf.set('HAVE_ADT', have_adt)
conf.set('HAVE_LIBAUDIT', libaudit_dep.found())
conf.set('HAVE_UTMP_H', have_utmp_header)
conf.set('HAVE_UTMPX_H', have_utmpx_header)
conf.set('HAVE_POSIX_GETPWNAM_R', have_posix_getpwnam_r)
conf.set('UTMP', utmp_struct)
conf.set('HAVE_GETUTXENT', cc.has_function('getutxent'))
conf.set('HAVE_UPDWTMP', cc.has_function('updwtmp'))
conf.set('HAVE_UPDWTMPX', cc.has_function('updwtmpx'))
conf.set('HAVE_LOGIN', cc.has_function('login', args: '-lutil'))
conf.set('HAVE_LOGOUT', cc.has_function('logout', args: '-lutil'))
conf.set('HAVE_LOGWTMP', cc.has_function('logwtmp', args: '-lutil'))
conf.set('HAVE_PAM_SYSLOG', have_pam_syslog)
conf.set('HAVE_KEYUTILS', keyutils_dep.found())
conf.set('SUPPORTS_PAM_EXTENSIONS', pam_extensions_supported)
conf.set('HAVE_SELINUX', libselinux_dep.found())
conf.set('HAVE_XSERVER_WITH_LISTEN', xserver_has_listen)
conf.set('ENABLE_USER_DISPLAY_SERVER', get_option('user-display-server'))
conf.set('ENABLE_SYSTEMD_JOURNAL', get_option('systemd-journal'))
conf.set('ENABLE_WAYLAND_SUPPORT', get_option('wayland-support'))
conf.set('ENABLE_X11_SUPPORT', get_option('x11-support'))
conf.set('ENABLE_PROFILING', get_option('profiling'))
conf.set('GDM_INITIAL_VT', get_option('initial-vt'))
conf.set_quoted('GDM_DEFAULTS_CONF', gdm_defaults_conf)
conf.set_quoted('GDM_CUSTOM_CONF', gdm_custom_conf)
conf.set_quoted('GDM_RUNTIME_CONF', gdm_runtime_conf)
conf.set_quoted('GDM_SESSION_DEFAULT_PATH', get_option('default-path'))
conf.set_quoted('GDM_USERNAME', get_option('user'))
conf.set_quoted('GDM_GROUPNAME', get_option('group'))
conf.set('HAVE_LIBXDMCP', have_xdmcp)
conf.set_quoted('SYSTEMD_X_SERVER', systemd_x_server)
conf.set('HAVE_USERDB', have_userdb)
conf.set('GREETER_UID_MIN', greeter_uid_min)
conf.set('GREETER_UID_MAX', greeter_uid_max)
conf.set_quoted('GDM_DYN_HOME_DIR', gdm_dyn_home_dir)
conf.set_quoted('NOLOGIN_PATH', nologin_path)
conf.set('WITH_PLYMOUTH', plymouth_dep.found())
conf.set_quoted('X_SERVER', x_bin)
conf.set_quoted('X_PATH', x_path)
conf.set('HAVE_UDEV', gudev_dep.found())
conf.set('HAVE_UT_UT_HOST', utmp_has_host_field)
conf.set('HAVE_UT_UT_PID', utmp_has_pid_field)
conf.set('HAVE_UT_UT_ID', utmp_has_id_field)
conf.set('HAVE_UT_UT_NAME', utmp_has_name_field)
conf.set('HAVE_UT_UT_TYPE', utmp_has_type_field)
conf.set('HAVE_UT_UT_EXIT_E_TERMINATION', utmp_has_exit_e_termination_field)
conf.set('HAVE_UT_UT_USER', utmp_has_user_field)
conf.set('HAVE_UT_UT_TIME', utmp_has_time_field)
conf.set('HAVE_UT_UT_TV', utmp_has_tv_field)
conf.set('HAVE_UT_UT_SYSLEN', utmp_has_syslen_field)
conf.set('ENABLE_IPV6', get_option('ipv6'))
conf.set('HAVE_SYS_SOCKIO_H', cc.has_header('sys/sockio.h'))
configure_file(output: 'config.h', configuration: conf)

# Subdirs
subdir('data')
subdir('common')
if pam_extensions_supported
  subdir('pam-extensions')
endif
subdir('daemon')
subdir('libgdm')
subdir('utils')
subdir('pam_gdm')
subdir('po')
if libcheck_dep.found()
  subdir('tests')
endif
if have_xdmcp
  subdir('chooser')
endif
subdir('docs')

gnome.post_install(
  glib_compile_schemas: true,
)

summary({
    'datadir': gdm_prefix / get_option('datadir'),
    'sysconfdir': gdm_prefix / get_option('sysconfdir'),
    'bindir': gdm_prefix / get_option('bindir'),
    'libdir': gdm_prefix / get_option('libdir'),
    'libexecdir': gdm_prefix / get_option('libexecdir'),
    'logdir': get_option('log-dir'),
    'System DBus': dbus_sys_dir,
    'DM conf': dmconfdir,
    'GDM conf': gdmconfdir,
    'GDM Xauth': gdm_xauth_dir,
    'GDM Run': gdm_run_dir,
    'GDM Dynamic Homes': gdm_dyn_home_dir,
    'GDM PID file': gdm_pid_file,
    'GDM Defaults config': gdm_defaults_conf,
    'GDM Custom config': gdm_custom_conf,
    'GDM Runtime config': gdm_runtime_conf,
    'GDM Session default path': get_option('default-path'),
    'GDM Working dir': working_dir,
    'GNOME locale': gdm_prefix / get_option('localedir'),
    'GNOME Settings Daemon': gnome_settings_daemon_dir,
    'AT SPI registryd': at_spi_registryd_dir,
    'Lang config file': lang_config_file,
    'Pam MOD': pam_mod_dir,
    'Udev rules': udev_dir,
    'XServer': x_bin,
    'XPath': x_path,
    'Systemd XServer': systemd_x_server,
  },
  section: 'Paths',
)

summary({
    'ADT': have_adt,
    'PAM Syslog': have_pam_syslog,
    'Supports PAM Extensions': pam_extensions_supported,
    'SeLinux': libselinux_dep.found(),
    'Logind Provider': get_option('logind-provider'),
    'Use GDM Xsession': get_option('gdm-xsession'),
    'Use UserDisplayServer': get_option('user-display-server'),
    'Use SystemdJournal': get_option('systemd-journal'),
    'Use WaylandSupport': get_option('wayland-support'),
    'Use X11Support': have_x11_support,
    'Use Profiling': get_option('profiling'),
    'Initial VT': get_option('initial-vt'),
    'Username': get_option('user'),
    'Groupname': get_option('group'),
    'Plymouth': plymouth_dep.found(),
    'Use ipv6': get_option('ipv6'),
  },
  section: 'Features',
  bool_yn: true,
)
