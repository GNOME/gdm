project('gdm', 'c',
  version: '3.30.2',
  license: 'GPL2+',
  meson_version: '>= 0.47',
)

# Modules
gnome = import('gnome')
i18n = import('i18n')

# Compiler
cc = meson.get_compiler('c')

# Common options
gdm_prefix = get_option('prefix')
gdm_datadir = join_paths(gdm_prefix, get_option('datadir'))
gdm_includedir = join_paths(gdm_prefix, get_option('includedir'))
gdm_libdir = join_paths(gdm_prefix, get_option('libdir'))
gdm_libexecdir = join_paths(gdm_prefix, get_option('libexecdir'))
gdm_bindir = join_paths(gdm_prefix, get_option('bindir'))
gdm_sysconfdir = join_paths(gdm_prefix, get_option('sysconfdir'))
gdm_localstatedir = join_paths(gdm_prefix, get_option('localstatedir'))
gnome_localedir = join_paths(gdm_datadir, 'localedir')

# GDM-specifc Options
selinux_enabled = get_option('selinux')
gdmconfdir = (get_option('sysconfsubdir') == '')? gdm_sysconfdir : join_paths(gdm_sysconfdir, get_option('sysconfsubdir'))
dmconfdir = (get_option('dmconfdir') != '')? get_option('dmconfdir') : join_paths(gdm_sysconfdir, 'dm')
install_gdm_xsession = get_option('gdm-xsession')
user_display_server_enabled = get_option('user-display-server')
default_pam_config = get_option('default-pam-config')
console_helper_enabled = get_option('console-helper')
authentication_scheme_enabled = get_option('authentication-scheme')
xinerama_enabled = get_option('xinerama')
xdmcp_enabled = get_option('xdmcp')
tcp_wrappers_enabled = get_option('tcp-wrappers')
systemd_systemunitdir = get_option('systemdsystemunitdir')
systemd_journal_enabled = get_option('systemd-journal')
wayland_support_enabled = get_option('wayland-support')
plymouth_enabled = get_option('plymouth')
at_spi_registryd_dir = (get_option('at-spi-registryd-dir') != '')? get_option('at-spi-registryd-dir') : gdm_libexecdir
gdm_session_default_path = get_option('default-path')
lang_config_file = (get_option('lang-file') != '')? get_option('lang-file') : join_paths(gdm_sysconfdir, 'locale.conf')
ipv6_enabled = get_option('ipv6')
gdm_username = get_option('user')
gdm_groupname = get_option('group')
pam_prefix = (get_option('pam-prefix') != '')? get_option('pam-prefix') : gdm_sysconfdir
pam_mod_dir = (get_option('pam-mod-dir') != '')? get_option('pam-mod-dir') : join_paths(gdm_libdir, 'security')
dbus_sys = get_option('dbus-sys')
dbus_services = get_option('dbus-services')
rbac_shutdown_enabled = get_option('rbac-shutdown')
gdm_log_dir = get_option('log-dir')
at_bindir = (get_option('at-bindir') != '')? get_option('at-bindir') : gdm_bindir
gdm_defaults_conf = (get_option('defaults-conf') != '')? get_option('defaults-conf') : join_paths(gdm_datadir, 'gdm', 'defaults.conf')
gdm_custom_conf = (get_option('custom-conf') != '')? get_option('custom-conf') : join_paths(gdmconfdir, 'custom.conf')
gdm_runtime_conf = (get_option('runtime-conf') != '')? get_option('runtime-conf') : gdm_custom_conf
gnome_settings_daemon_dir = (get_option('gnome-settings-daemon-dir') != '')? get_option('gnome-settings-daemon-dir') : gdm_libexecdir
check_accelerated_dir = (get_option('check-accelerated-dir') != '')? get_option('check-accelerated-dir') : gdm_libexecdir
xevie_enabled = get_option('xevie')
libaudit_enabled = get_option('libaudit')
gdm_run_dir = (get_option('run-dir') != '')? get_option('run-dir') : join_paths(gdm_localstatedir, 'run', 'gdm')
gdm_pid_file = (get_option('pid-file') != '')? get_option('pid-file') : join_paths(gdm_run_dir, 'gdm.pid')
ran_once_marker_dir = (get_option('ran-once-marker-dir') != '')? get_option('ran-once-marker-dir') : gdm_run_dir
working_dir = (get_option('working-dir') != '')? get_option('working-dir') : join_paths(gdm_localstatedir, 'lib', 'gdm')
gdm_xauth_dir = (get_option('xauth-dir') != '')? get_option('xauth-dir') : gdm_run_dir
gdm_screenshot_dir = (get_option('screenshot-dir') != '')? get_option('screenshot-dir') : join_paths(gdm_run_dir, 'greeter')
profiling_enabled = get_option('profiling')
gdm_initial_vt = get_option('initial-vt')

# Common variables
config_h_dir = include_directories('.')

# Dependencies
glib_min_version = '2.44.0'

glib_dep = dependency('glib-2.0', version: '>=' + glib_min_version)
gobject_dep = dependency('gobject-2.0', version: '>=' + glib_min_version)
gio_dep = dependency('gio-2.0', version: '>=' + glib_min_version)
gio_unix_dep = dependency('gio-unix-2.0', version: '>=' + glib_min_version)
gtk_dep = dependency('gtk+-3.0', version: '>= 2.91.1')
libcanberra_gtk_dep = dependency('libcanberra-gtk3', version: '>= 0.4')
accountsservice_dep = dependency('accountsservice', version: '>= 0.6.35')
xcb_dep = dependency('xcb')
keyutils_dep = cc.find_library('keyutils', required: false)
if selinux_enabled
  libselinux_dep = dependency('libselinux')
endif
xdmcp_dep = cc.find_library('Xdmcp', required: xdmcp_enabled)
# X11
x_deps = declare_dependency(
  dependencies: [
    dependency('x11'),
    dependency('xau'),
  ],
)
find_x_server_script = find_program('build-aux/find-x-server.sh', native: true)
find_x_server_out = run_command(find_x_server_script).stdout().strip()
if find_x_server_out != ''
  x_bin = find_x_server_out
  x_bin_path_split = x_bin.split('/')
  i = 0
  x_path = '/'
  foreach dir : x_bin_path_split
    if i < x_bin_path_split.length() - 1
      x_path = join_paths(x_path, dir)
    endif
    i = i + 1
  endforeach
else
  # what to do, what to do, this is wrong, but this just sets the
  # defaults, perhaps this user is cross compiling or some such
  x_path = '/usr/bin/X11:/usr/X11R6/bin:/opt/X11R6/bin'
  x_bin = '/usr/bin/X'
endif
# systemd
libsystemd_dep = dependency('libsystemd')
journald_dep = dependency('journald', required: false)
systemd_multiseat_x = find_program('systemd-multi-seat-x', required: false)
systemd_x_server = systemd_multiseat_x.found()? systemd_multiseat_x.path() : '/lib/systemd/systemd-multi-seat-x'
# Plymouth
plymouth_dep = dependency('ply-boot-client', required: plymouth_enabled)
# unit tests
libcheck_dep = dependency('check', version: '>= 0.9.4')

cc.has_header('security/pam_modutil.h')
cc.has_header('security/pam_ext.h')
have_pam_syslog = cc.has_function('pam_syslog', args: 'pam')

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

# PAM
libpam_dep = cc.find_library('pam')
pam_extensions_supported = cc.has_header_symbol('security/pam_appl.h', 'PAM_BINARY_PROMPT')

# Configuration
conf = configuration_data()
conf.set_quoted('G_LOG_DOMAIN', 'Gdm')
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('DATADIR', gdm_datadir)
conf.set_quoted('BINDIR', gdm_bindir)
conf.set_quoted('LIBDIR', gdm_libdir)
conf.set_quoted('LIBEXECDIR', gdm_libexecdir)
conf.set_quoted('LOGDIR', gdm_log_dir)
conf.set_quoted('DMCONFDIR', dmconfdir)
conf.set_quoted('GDMCONFDIR', gdmconfdir)
conf.set_quoted('GDM_SCREENSHOT_DIR', gdm_screenshot_dir)
conf.set_quoted('GDM_CACHE_DIR', join_paths(gdm_localstatedir, 'cache', 'gdm'))
conf.set_quoted('GDM_XAUTH_DIR', gdm_xauth_dir)
conf.set_quoted('GDM_RAN_ONCE_MARKER_DIR', ran_once_marker_dir)
conf.set_quoted('GDM_RUN_DIR', gdm_run_dir)
conf.set_quoted('GNOMELOCALEDIR', gnome_localedir)
conf.set_quoted('AT_SPI_REGISTRYD_DIR', at_spi_registryd_dir)
conf.set_quoted('GDM_PID_FILE', gdm_pid_file)
conf.set_quoted('GNOME_SETTINGS_DAEMON_DIR', gnome_settings_daemon_dir)
conf.set_quoted('CHECK_ACCELRATED_DIR', check_accelerated_dir)
conf.set_quoted('LANG_CONFIG_FILE', lang_config_file)
conf.set('HAVE_UTMP_H', have_utmp_header)
conf.set('HAVE_UTMPX_H', have_utmpx_header)
conf.set('HAVE_POSIX_GETPWNAM_R', have_posix_getpwnam_r)
conf.set('UTMP', utmp_struct)
conf.set('HAVE_GETUTXENT', cc.has_function('getutxent'))
conf.set('HAVE_UPDWTMP', cc.has_function('updwtmp'))
conf.set('HAVE_UPDWTMPX', cc.has_function('updwtmpx'))
conf.set('HAVE_LOGIN', cc.has_function('login', args: 'util'))
conf.set('HAVE_LOGOUT', cc.has_function('logout', args: 'util'))
conf.set('HAVE_LOGWTMP', cc.has_function('logwtmp', args: 'util'))
conf.set('HAVE_PAM_SYSLOG', have_pam_syslog)
conf.set('HAVE_KEYUTILS', keyutils_dep.found())
conf.set('SUPPORTS_PAM_EXTENSIONS', pam_extensions_supported)
conf.set('HAVE_SELINUX', selinux_enabled)
conf.set('ENABLE_USER_DISPLAY_SERVER', user_display_server_enabled)
conf.set('ENABLE_WAYLAND_SUPPORT', wayland_support_enabled)
conf.set_quoted('GDM_INITIAL_VT', '@0@'.format(gdm_initial_vt))
conf.set_quoted('GDM_DEFAULTS_CONF', gdm_custom_conf)
conf.set_quoted('GDM_CUSTOM_CONF', gdm_custom_conf)
conf.set_quoted('GDM_RUNTIME_CONF', gdm_runtime_conf)
conf.set_quoted('GDM_SESSION_DEFAULT_PATH', gdm_session_default_path)
conf.set_quoted('GDM_USERNAME', gdm_username)
conf.set_quoted('GDM_GROUPNAME', gdm_groupname)
conf.set('HAVE_LIBXDMCP', xdmcp_dep.found())
conf.set_quoted('SYSTEMD_X_SERVER', systemd_x_server)
conf.set('WITH_PLYMOUTH', plymouth_dep.found())
conf.set_quoted('X_SERVER', x_bin)
conf.set_quoted('X_PATH', x_path)
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
subdir('tests')
# if xdmcp_enabled
  subdir('chooser')
# endif
# if docs_enabled
#   subdir('docs')
# endif
