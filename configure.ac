
AC_PREREQ([2.60])
AC_INIT([gdm],
        [3.26.1],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gdm])

AC_CONFIG_SRCDIR([daemon/gdm-manager.c])

AM_INIT_AUTOMAKE([1.11.2 no-dist-gzip dist-xz tar-ustar -Wall])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_MAINTAINER_MODE([enable])

# for utmpx stuff
AC_GNU_SOURCE

AC_STDC_HEADERS
AC_PROG_CXX
AM_PROG_CC_C_O
AC_PROG_LIBTOOL()

## increment if the plugin interface has additions, changes, removals.
LT_CURRENT=1

## increment any time the source changes; set to
##  0 if you increment CURRENT
LT_REVISION=0

## increment if any interfaces have been added; set to 0
## if any interfaces have been changed or removed. removal has
## precedence over adding, so set to 0 if both happened.
LT_AGE=0

AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)

AC_HEADER_STDC

AC_SUBST(VERSION)

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_MACRO_DIR([m4])

# Documentation
enable_documentation=no
m4_ifdef([YELP_HELP_INIT],[
YELP_HELP_INIT
enable_documentation=yes
])
AM_CONDITIONAL(ENABLE_DOCUMENTATION, test x$enable_documentation = xyes)

# i18n stuff
IT_PROG_INTLTOOL([0.40.0])

GETTEXT_PACKAGE=gdm
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [gettext package])

dnl ---------------------------------------------------------------------------
dnl - Dependencies
dnl ---------------------------------------------------------------------------

GLIB_REQUIRED_VERSION=2.36.0
GTK_REQUIRED_VERSION=2.91.1
LIBCANBERRA_GTK_REQUIRED_VERSION=0.4
ACCOUNTS_SERVICE_REQUIRED_VERSION=0.6.35

EXTRA_COMPILE_WARNINGS(yes)

PKG_CHECK_MODULES(GTHREAD, gthread-2.0)
AC_SUBST(GTHREAD_CFLAGS)
AC_SUBST(GTHREAD_LIBS)

PKG_CHECK_MODULES(COMMON,
        gobject-2.0 >= $GLIB_REQUIRED_VERSION
        gio-2.0 >= $GLIB_REQUIRED_VERSION
        gio-unix-2.0 >= $GLIB_REQUIRED_VERSION
)
AC_SUBST(COMMON_CFLAGS)
AC_SUBST(COMMON_LIBS)

PKG_CHECK_MODULES(DAEMON,
        gobject-2.0 >= $GLIB_REQUIRED_VERSION
        gio-2.0 >= $GLIB_REQUIRED_VERSION
        gio-unix-2.0 >= $GLIB_REQUIRED_VERSION
        accountsservice >= $ACCOUNTS_SERVICE_REQUIRED_VERSION
        xcb
)
AC_SUBST(DAEMON_CFLAGS)
AC_SUBST(DAEMON_LIBS)

GLIB_GSETTINGS

PKG_CHECK_MODULES(XLIB, x11 xau, ,
  [AC_PATH_XTRA
    if test "x$no_x" = xyes; then
      AC_MSG_ERROR("no (requires X development libraries)")
    else
      XLIB_LIBS="$X_PRE_LIBS $X_LIBS -lXau -lX11 -lXext $X_EXTRA_LIBS"
      XLIB_CFLAGS=$X_CFLAGS
    fi])
AC_SUBST(XLIB_CFLAGS)
AC_SUBST(XLIB_LIBS)

PKG_CHECK_MODULES(GTK,
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)

PKG_CHECK_MODULES(CANBERRA_GTK,
        libcanberra-gtk3 >= $LIBCANBERRA_GTK_REQUIRED_VERSION
)
AC_SUBST(CANBERRA_GTK_CFLAGS)
AC_SUBST(CANBERRA_GTK_LIBS)

AC_ARG_WITH(selinux,
            AS_HELP_STRING([--with-selinux],
                           [Add SELinux support]),,
            with_selinux=auto)

PKG_CHECK_MODULES(LIBSELINUX, libselinux, have_selinux=yes, have_selinux=no)

use_selinux=no
if test "x$have_selinux" = "xyes" && test "x$with_selinux" != "xno" ; then
        AC_DEFINE(HAVE_SELINUX, 1, [Define if have selinux])
        use_selinux=yes
fi
AC_SUBST(LIBSELINUX_CFLAGS)
AC_SUBST(LIBSELINUX_LIBS)

PKG_CHECK_MODULES(SIMPLE_CHOOSER,
        gtk+-3.0 >= $GTK_REQUIRED_VERSION
)
AC_SUBST(SIMPLE_CHOOSER_CFLAGS)
AC_SUBST(SIMPLE_CHOOSER_LIBS)

PLUGIN_LIBTOOL_FLAGS="-export_dynamic -module -avoid-version"
AC_SUBST(PLUGIN_LIBTOOL_FLAGS)

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_XTRA

# Unit testing framework
PKG_CHECK_MODULES(CHECK,
                  [check >= 0.9.4],
                  have_check=yes,
                  have_check=no)
AM_CONDITIONAL([HAVE_CHECK],[test "x$CHECK_CFLAGS" != "x"])

PKG_CHECK_MODULES(LIBGDM, glib-2.0 gio-2.0 gio-unix-2.0);
GOBJECT_INTROSPECTION_CHECK([0.9.12])
LIBGDM_GIR_INCLUDES="GLib-2.0 GObject-2.0 Gio-2.0"
AC_SUBST(LIBGDM_GIR_INCLUDES)

dnl ---------------------------------------------------------------------------
dnl - Configuration file stuff
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(sysconfsubdir,
            AS_HELP_STRING([--with-sysconfsubdir],
                           [directory name used under sysconfdir, default=gdm]),
            sysconfsubdir=${withval}, sysconfsubdir=gdm)
AC_SUBST(sysconfsubdir)

if test x"${sysconfsubdir}" != xno -a x"${sysconfsubdir}" != x; then
    gdmconfdir='${sysconfdir}/${sysconfsubdir}'
else
    gdmconfdir='${sysconfdir}'
fi
AC_SUBST(gdmconfdir)

AC_ARG_WITH(dmconfdir,
            AS_HELP_STRING([--with-dmconfdir],
                           [directory where Sessions are stored, default=SYSCONFDIR/dm]),
            dmconfdir=${withval}, dmconfdir=${sysconfdir}/dm)
AC_SUBST(dmconfdir)

dnl ---------------------------------------------------------------------------
dnl - Configure arguments
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE(gdm-xsession,
              AS_HELP_STRING([--enable-gdm-xsession],
                             [Enable installing the gdm Xsession file @<:@default=no@:>@]),,
              enable_gdm_xsession=no)
AM_CONDITIONAL(ENABLE_GDM_XSESSION, test x$enable_gdm_xsession = xyes)

AC_ARG_ENABLE(split-authentication,
	      AS_HELP_STRING([--enable-split-authentication],
                             [Enable multiple simultaneous PAM conversations during login @<:@default=yes@:>@]),,
              enable_split_authentication=yes)
AM_CONDITIONAL(ENABLE_SPLIT_AUTHENTICATION, test x$enable_split_authentication = xyes)

if test x$enable_split_authentication = xyes; then
  AC_DEFINE(ENABLE_SPLIT_AUTHENTICATION, 1, [Define if split authentication is enabled])
fi

AC_ARG_ENABLE(user-display-server,
	      AS_HELP_STRING([--enable-user-display-server],
                             [Enable running X server as user @<:@default=yes@:>@]),,
              enable_user_display_server=yes)
AM_CONDITIONAL(ENABLE_USER_DISPLAY_SERVER, test x$user_display_server = xyes)

if test x$enable_user_display_server = xyes; then
  AC_DEFINE(ENABLE_USER_DISPLAY_SERVER, 1, [Define if user display servers are supported])
fi

AC_ARG_WITH(default-pam-config,
	    AS_HELP_STRING([--with-default-pam-config: One of redhat, openembedded, exherbo, lfs, arch, none @<:@default=auto@:>@]))
dnl If not given, try autodetecting from release files (see NetworkManager source) 
if test x$with_default_pam_config = x; then
	AC_CHECK_FILE(/etc/redhat-release,with_default_pam_config="redhat")
	AC_CHECK_FILE(/etc/fedora-release,with_default_pam_config="redhat")
	AC_CHECK_FILE(/etc/exherbo-release,with_default_pam_config="exherbo")
	AC_CHECK_FILE(/etc/arch-release,with_default_pam_config="arch")
	AC_CHECK_FILE(/etc/lfs-release,with_default_pam_config="lfs")
	dnl If not autodetected, default to none
	if test x$with_default_pam_config = x; then
	  with_default_pam_config=none
	fi
fi
case x$with_default_pam_config in
     xredhat|xopenembedded|xexherbo|xlfs|xarch|xnone) ;;
     *)
       AC_MSG_ERROR([Invalid --with-default-pam-config ${with_default_pam_config}])
       exit 1
       ;;
esac
AM_CONDITIONAL(ENABLE_REDHAT_PAM_CONFIG, test x$with_default_pam_config = xredhat)
AM_CONDITIONAL(ENABLE_OPENEMBEDDED_PAM_CONFIG, test x$with_default_pam_config = xopenembedded)
AM_CONDITIONAL(ENABLE_EXHERBO_PAM_CONFIG, test x$with_default_pam_config = xexherbo)
AM_CONDITIONAL(ENABLE_LFS_PAM_CONFIG, test x$with_default_pam_config = xlfs)
AM_CONDITIONAL(ENABLE_ARCH_PAM_CONFIG, test x$with_default_pam_config = xarch)

AC_CHECK_HEADERS([security/pam_modules.h security/pam_modutil.h security/pam_ext.h],
                 [have_pam=yes],
                 [if test "x$have_pam" = xyes ; then
                        AC_MSG_ERROR([PAM development files not found.])
                 fi])

AC_ARG_ENABLE(console-helper,
	      AS_HELP_STRING([--enable-console-helper],
                             [Enable PAM console helper @<:@default=auto@:>@]),,
              enable_console_helper=auto)

AC_ARG_ENABLE(authentication-scheme,
              AS_HELP_STRING([--enable-authentication-scheme=@<:@pam/crypt/shadow@:>@],
	                     [Choose a specific authentication scheme @<:@default=auto@:>@]),,
              enable_authentication_scheme=auto)

AC_ARG_WITH(xinerama,
            AS_HELP_STRING([--with-xinerama],
                           [Add Xinerama support @<:@default=auto@:>@]),,
            with_xinerama=auto)

AC_ARG_WITH(xdmcp,
            AS_HELP_STRING([--with-xdmcp],
	                   [Add XDMCP (remote login) support @<:@default=auto@:>@]),,
            with_xdmcp=auto)

AC_ARG_WITH(tcp-wrappers,
            AS_HELP_STRING([--with-tcp-wrappers],
                           [Use TCP Wrappers @<:@default=auto@:>@]),,
            with_tcp_wrappers=auto)


AC_ARG_WITH([systemdsystemunitdir],
            AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
                           [Directory for systemd service files]),
                [with_systemdsystemunitdir=$withval], [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
AC_ARG_ENABLE(systemd-journal,
            AS_HELP_STRING([--enable-systemd-journal],
                           [Add journald support @<:@default=auto@:>@]),
            [enable_systemd_journal=$enableval], [enable_systemd_journal=auto])
AC_ARG_ENABLE(wayland-support,
	      AS_HELP_STRING([--enable-wayland-support],
                             [Enable support for wayland sessions  @<:@default=auto@:>@]),
              [enable_wayland_support=$enableval],
              [enable_wayland_support=yes])

AC_ARG_WITH(plymouth,
            AS_HELP_STRING([--with-plymouth],
                           [Add plymouth support @<:@default=yes@:>@]),,
                           [with_plymouth=yes])

AC_ARG_WITH(at-spi-registryd-directory,
            AS_HELP_STRING([--with-at-spi-registryd-directory],
	                   [Specify the directory of at-spi-registryd @<:@default=libexecdir@:>@])],,
            [with_at_spi_registryd_directory="${libexecdir}"])

AT_SPI_REGISTRYD_DIR=$with_at_spi_registryd_directory
AC_SUBST(AT_SPI_REGISTRYD_DIR)

# Allow configuration of default PATH
#
withval=""
AC_ARG_WITH(default-path,
            AS_HELP_STRING([--with-default-path=<PATH>],
	                   [PATH GDM will use as the user's default PATH]),
            [if test x$withval != x; then
               AC_MSG_RESULT("PATH ${withval} will be the default PATH.")
             fi])

if test x$withval != x; then
	GDM_SESSION_DEFAULT_PATH="$withval"
else
	GDM_SESSION_DEFAULT_PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin"
fi
AC_SUBST(GDM_SESSION_DEFAULT_PATH)

dnl
dnl file that sets LANG
dnl
withval=""
AC_ARG_WITH(lang-file,
            AS_HELP_STRING([--with-lang-file=<filename>],
	                   [file containing default language setting]),
            [if test x$withval != x; then
               AC_MSG_RESULT("System locale will be looked for in file ${withval}.")
             fi])

if test x$withval != x; then
	LANG_CONFIG_FILE="$withval"
else
	LANG_CONFIG_FILE='$(sysconfdir)/locale.conf'
fi
AC_SUBST(LANG_CONFIG_FILE)

# stropts has been removed from glibc
# https://bugzilla.redhat.com/show_bug.cgi?id=436349
AC_CHECK_HEADERS(stropts.h)

dnl socklen_t may be declared, but not in a "standard" C header location
AC_CHECK_HEADERS(sys/socket.h)
AC_CHECK_TYPE(socklen_t,,
        AC_DEFINE(socklen_t, size_t, [Compatibility type]),
[AC_INCLUDES_DEFAULT]
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
)
AC_CHECK_HEADERS(sys/sockio.h)
AC_CHECK_FUNCS([setresuid setenv unsetenv clearenv])

dnl checks needed for Darwin compatibility to linux **environ.
AC_CHECK_HEADERS(crt_externs.h)
AC_CHECK_FUNCS(_NSGetEnviron)

AC_CHECK_HEADERS(sys/vt.h)

EXTRA_DAEMON_LIBS=""
EXTRA_CHOOSER_LIBS=""
EXTRA_XNEST_LIBS=""
EXTRA_FLEXI_LIBS=""
EXTRA_DYNAMIC_LIBS=""
EXTRA_SETUP_LIBS=""
EXTRA_TEST_LIBS=""

AC_CHECK_FUNC(socket,,[
	      AC_CHECK_LIB(socket,socket, [
			   EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lsocket"
			   EXTRA_CHOOSER_LIBS="$EXTRA_CHOOSER_LIBS -lsocket"
			   EXTRA_XNEST_LIBS="$EXTRA_XNEST_LIBS -lsocket"
			   EXTRA_FLEXI_LIBS="$EXTRA_FLEXI_LIBS -lsocket"
			   EXTRA_DYNAMIC_LIBS="$EXTRA_DYNAMIC_LIBS -lsocket"
			   EXTRA_SETUP_LIBS="$EXTRA_SETUP_LIBS -lsocket"
			   EXTRA_TEST_LIBS="$EXTRA_TEST_LIBS -lsocket"])])
AC_CHECK_FUNC(gethostbyname,,[
	      AC_CHECK_LIB(nsl,gethostbyname, [
			   EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lnsl"
			   EXTRA_CHOOSER_LIBS="$EXTRA_CHOOSER_LIBS -lnsl"
			   EXTRA_TEST_LIBS="$EXTRA_TEST_LIBS -lnsl"])])
AC_CHECK_DECL(HOST_NAME_MAX,,[
             AC_CHECK_DECL(_POSIX_HOST_NAME_MAX,
                           AC_DEFINE(HOST_NAME_MAX, _POSIX_HOST_NAME_MAX, []),
                           AC_DEFINE(HOST_NAME_MAX, 256, [Define to 256 if neither have HOST_NAME_MAX nor _POSIX_HOST_NAME_MAX]),
                           [[#include <limits.h>]])],
             [[#include <limits.h>]])
AC_CHECK_FUNC(sched_yield,[
	      AC_DEFINE(HAVE_SCHED_YIELD, 1, [Define if we have sched yield])],[
	      AC_CHECK_LIB(rt,sched_yield, [
			   AC_DEFINE(HAVE_SCHED_YIELD, 1, [Define if we have sched yield])
			   EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lrt"], [
			   echo "No sched_yield found"])])
AC_CHECK_FUNC(inet_aton,,[
	      AC_CHECK_LIB(resolv,inet_aton, [
			   EXTRA_CHOOSER_LIBS="$EXTRA_CHOOSER_LIBS -lresolv"])])

dnl ---------------------------------------------------------------------------
dnl - Check for mkdtemp
dnl ---------------------------------------------------------------------------

mkdtemp_missing=false
AC_CHECK_FUNC(mkdtemp,
    [AC_DEFINE([HAVE_MKDTEMP], 1, [Have GlibC function to make temp dirs])],
    mkdtemp_missing=true)
AM_CONDITIONAL(MKDTEMP_MISSING, test x$mkdtemp_missing = xtrue)

dnl ---------------------------------------------------------------------------
dnl - Check for IPv6
dnl ---------------------------------------------------------------------------

AC_MSG_CHECKING([whether to enable IPv6])
AC_ARG_ENABLE(ipv6,
              AS_HELP_STRING([--enable-ipv6],
                             [Enables compilation of IPv6 code @<:@default=no@:>@]),,
              enable_ipv6=no)

if test x$enable_ipv6 = xyes; then
  AC_TRY_COMPILE([
    #include <sys/types.h>
    #include <sys/socket.h>],[
    struct sockaddr_storage ss;
    socket(AF_INET6, SOCK_STREAM, 0)
    ],
    have_ipv6=yes,
    have_ipv6=no
    )
  if test x$have_ipv6 = xyes; then
    have_getaddrinfo=no
    have_inet_ntop=no
    AC_CHECK_FUNC(getaddrinfo, have_getaddrinfo=yes)
    if test x$have_getaddrinfo != xyes; then
      for lib in bsd socket inet; do
        AC_CHECK_LIB($lib,getaddrinfo,["LIBS=$LIBS -l$lib";have_getaddrinfo=yes;break])
      done
    fi

    AC_CHECK_FUNC(inet_ntop,have_inet_ntop=yes)
    if test x$have_inet_ntop != xyes; then
      for lib in bsd nsl inet; do
        AC_CHECK_LIB($lib,inet_ntop,["LIBS=$LIBS -l$lib";have_inet_ntop=yes;break])
      done
    fi

    if test x$have_getaddrinfo = xyes; then
      if test x$have_inet_ntop = xyes; then
        have_ipv6=yes
        AC_DEFINE(ENABLE_IPV6, 1, [Define if we have ipv6])
        AC_MSG_RESULT($have_ipv6)
      else
        have_ipv6=no
        AC_MSG_RESULT($have_ipv6)
      fi
    fi
  fi
else
  AC_MSG_RESULT(no)
fi

dnl ---------------------------------------------------------------------------
dnl - Check for GDM user
dnl ---------------------------------------------------------------------------

withval=""
AC_ARG_WITH(user,
            AS_HELP_STRING([--with-user=<username>],
                           [specify gdm's user name]))

if test x$withval != x; then
	GDM_USERNAME="$withval"
else
	GDM_USERNAME=gdm
fi
AC_SUBST(GDM_USERNAME)
AC_DEFINE_UNQUOTED([GDM_USERNAME], "$GDM_USERNAME", [User to use])

dnl ---------------------------------------------------------------------------
dnl - Check for GDM group
dnl ---------------------------------------------------------------------------

withval=""
AC_ARG_WITH(group,
            AS_HELP_STRING([--with-group=<group>],
                           [specify gdm's group name]))

if test x$withval != x; then
	GDM_GROUPNAME="$withval"
else
	GDM_GROUPNAME=gdm
fi
AC_SUBST(GDM_GROUPNAME)
AC_DEFINE_UNQUOTED([GDM_GROUPNAME], "$GDM_GROUPNAME", [Group to use])


dnl ---------------------------------------------------------------------------
dnl - Check for PAM stuff
dnl ---------------------------------------------------------------------------

dnl PAM prefix (configuration files)
withval=""
AC_ARG_WITH(pam-prefix,
            AS_HELP_STRING([--with-pam-prefix=<prefix>],
                           [specify where pam files go]),
            [if test x$withval != x; then
               AC_MSG_RESULT("PAM files will be installed in prefix ${withval}.")
             fi])

if test x$withval != x; then
	PAM_PREFIX="$withval"
else
	PAM_PREFIX='${sysconfdir}'
fi
AC_SUBST(PAM_PREFIX)

dnl PAM dir (dynamic modules)
AC_ARG_WITH([pam-mod-dir],
            [AS_HELP_STRING([--with-pam-mod-dir=DIR],
                            [directory to install pam modules in])],
            [], [with_pam_mod_dir='${libdir}/security'])
PAM_MOD_DIR="$with_pam_mod_dir"
AC_SUBST(PAM_MOD_DIR)

have_pam=no
AC_CHECK_LIB(pam, pam_start, have_pam=yes)

if test "x$have_pam" = "xyes"; then
        PAM_LIBS="${PAM_LIBS} -lpam"
else
	AC_MSG_ERROR("PAM libraries not found")
fi
AC_SUBST(HAVE_PAM)
AC_SUBST(PAM_LIBS)

AC_CHECK_HEADERS([security/pam_modutil.h security/pam_ext.h])
AC_CHECK_LIB(pam, pam_syslog, [AC_DEFINE(HAVE_PAM_SYSLOG, [], [Define to 1 if you have the pam_syslog function])])

dnl test whether struct pam_message is const (Linux) or not (Sun)
if test "x$have_pam" = "xyes"; then
   pam_appl_h="$ac_pam_includes/security/pam_appl.h"
   AC_MSG_CHECKING(for const pam_message)
   AC_EGREP_HEADER([struct pam_message],
      $pam_appl_h,
      [ AC_EGREP_HEADER([const struct pam_message],
                        $pam_appl_h,
                        [AC_MSG_RESULT(["const: Linux-type PAM"]) ],
                        [AC_MSG_RESULT(["nonconst: Sun-type PAM"])
                        AC_DEFINE(PAM_MESSAGE_NONCONST, 1, [Define if your PAM support takes non-const arguments (Solaris)])]
                        )],
       [AC_MSG_RESULT(["not found - assume const, Linux-type PAM"])]
       )
   AC_CHECK_DECL(PAM_BINARY_PROMPT,
                 [supports_pam_extensions=yes],
                 [supports_pam_extensions=no],
                 #include <security/pam_appl.h>
                )
fi
if test "x$supports_pam_extensions" = "xyes" ; then
	AM_CONDITIONAL(SUPPORTS_PAM_EXTENSIONS, true)
	AC_DEFINE(SUPPORTS_PAM_EXTENSIONS, 1, [Define if PAM supports GDMs custom extensions])
fi

AC_CHECK_LIB(keyutils, keyctl_read, [
        AC_DEFINE(HAVE_KEYUTILS, 1, [Define if have keyutils])
        KEYUTILS_LIBS="-lkeyutils"
        KEYUTILS_CFLAGS=""
])
AC_SUBST(KEYUTILS_LIBS)
AC_SUBST(KEYUTILS_CFLAGS)

dnl Check if we can use the setpenv function to add specialvariable
dnl to the environment (such as the /etc/environment file under AIX)
AC_LINK_IFELSE([
	AC_LANG_PROGRAM([[
		#ifdef HAVE_USERSEC_H
		#include <usersec.h>
		#endif
		]],[[
		return (setpenv(NULL, (PENV_INIT | PENV_NOEXEC), NULL, NULL));
		]])],
	can_use_setpenv=yes,
	can_use_setpenv=no)

if test x$can_use_setpenv = xyes ; then
	AC_DEFINE(CAN_USE_SETPENV,,[Define this variable if the code to use
		the setpenv function can be compiled and used])
fi

#
# Can we use BSD's setusercontext
#
AC_CHECK_HEADER(login_cap.h, [
		EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lutil"
		AC_DEFINE(HAVE_LOGINCAP, 1, [Define if we have logincap])])

#
# Check for sys/sockio.h
#
AC_CHECK_HEADERS(sys/sockio.h, [
		 AC_DEFINE(HAVE_SYS_SOCKIO_H, 1, [Define if we have sys/sockio.h])])

#
# Check for libgen.h
#
AC_CHECK_HEADERS(libgen.h, [
		 AC_DEFINE(HAVE_LIBGEN_H, 1, [Define if we have libgen.h])])


dnl ---------------------------------------------------------------------------
dnl - Check for POSIX version of getpwnam_r
dnl ---------------------------------------------------------------------------

# Checking for a posix version of getpwnam_r
# if we are cross compiling and can not run the test
# assume getpwnam_r is the posix version
# it is up to the person cross compiling to change
# this behavior if desired

AC_LANG_PUSH(C)
AC_CACHE_CHECK([for posix getpwnam_r],
		ac_cv_func_posix_getpwnam_r,
		[AC_RUN_IFELSE([AC_LANG_PROGRAM(
[[
#define _POSIX_PTHREAD_SEMANTICS
#include <errno.h>
#include <pwd.h>
]],
[[
    char buffer[10000];
    struct passwd pwd, *pwptr = &pwd;
    int error;
    errno = 0;
    error = getpwnam_r ("", &pwd, buffer,
                        sizeof (buffer), &pwptr);
   return (error < 0 && errno == ENOSYS)
	   || error == ENOSYS;
]])],
	[ac_cv_func_posix_getpwnam_r=yes],
	[ac_cv_func_posix_getpwnam_r=no],
        [ac_cv_func_posix_getpwnam_r=yes]
)])
AC_LANG_POP(C)

if test "$ac_cv_func_posix_getpwnam_r" = yes; then
	AC_DEFINE(HAVE_POSIX_GETPWNAM_R,1,
		[Have POSIX function getpwnam_r])
else
	AC_CACHE_CHECK([for nonposix getpwnam_r],
		ac_cv_func_nonposix_getpwnam_r,
		[AC_TRY_LINK([#include <pwd.h>],
                      	[char buffer[10000];
                       	struct passwd pwd;
                       	getpwnam_r ("", &pwd, buffer, 
                               		sizeof (buffer));],
			[ac_cv_func_nonposix_getpwnam_r=yes],
			[ac_cv_func_nonposix_getpwnam_r=no])])
		if test "$ac_cv_func_nonposix_getpwnam_r" = yes; then
		AC_DEFINE(HAVE_NONPOSIX_GETPWNAM_R,1,
			[Have non-POSIX function getpwnam_r])
	fi
fi


dnl ---------------------------------------------------------------------------
dnl - Check for utmp stuff
dnl ---------------------------------------------------------------------------

AC_CHECK_HEADERS(utmp.h utmpx.h libutil.h sys/param.h)
AC_CHECK_FUNCS([getutxent updwtmpx updwtmp])
AC_CHECK_LIB(util, login, [
		   AC_DEFINE(HAVE_LOGIN, 1, [Define if have login])
		       EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lutil" ])
AC_CHECK_LIB(util, logout, [
		   AC_DEFINE(HAVE_LOGOUT, 1, [Define if have logout])
		       EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lutil" ])
AC_CHECK_LIB(util, logwtmp, [
	  	   AC_DEFINE(HAVE_LOGWTMP, 1, [Define if have logwtmp])
		       EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lutil" ])
GDM_CHECK_UTMP

AC_MSG_CHECKING(if utmpx structure has ut_syslen field)
AC_TRY_COMPILE([
  #include <utmpx.h>],[
  struct utmpx record;
  record.ut_syslen = sizeof (record.ut_host);
  ],
  have_ut_syslen=yes,
  have_ut_syslen=no
  )
if test x$have_ut_syslen = xyes; then
  AC_DEFINE(HAVE_UT_SYSLEN, 1, [Define if have ut_syslen record])
fi
AC_MSG_RESULT($have_ut_syslen)


dnl ---------------------------------------------------------------------------
dnl - Check for Xdmcp
dnl ---------------------------------------------------------------------------

XDMCP_LIBS=""
if test x$with_xdmcp != xno ; then
	xdmcp_save_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $X_CFLAGS"
	AC_CHECK_HEADER(X11/Xdmcp.h, [
		AC_CHECK_LIB(Xdmcp, XdmcpFlush, [
		     	AC_DEFINE(HAVE_LIBXDMCP, 1, [Define if have libxdmcp])
		     	XDMCP_LIBS="-lXdmcp"
		     	XDMCP_SUPPORT=yes],,[$X_LIBS -lX11 $X_EXTRA_LIBS])
	],,[#include <X11/Xproto.h>])

	if test x$with_xdmcp = xyes -a "x$XDMCP_SUPPORT" = "x" ; then
		AC_MSG_ERROR(XDMCP support requested but XDMCP libraries not found)
	fi
	CPPFLAGS="$xdmcp_save_CPPFLAGS"
fi
AC_SUBST(XDMCP_LIBS)

if test "x$XDMCP_SUPPORT" = "xyes" ; then
	AM_CONDITIONAL(XDMCP_SUPPORT, true)
	GDMCHOOSER=gdmchooser
else
        # No need for TCP Wrappers in case XDMCP is disabled
        if test x$with_tcp_wrappers != xno ; then
	        echo "TCP wrappers not needed if XDMCP is not enabled"
        fi
	with_tcp_wrappers=no
	AM_CONDITIONAL(XDMCP_SUPPORT, false)
	GDMCHOOSER=
fi
AC_SUBST(GDMCHOOSER)

dnl ---------------------------------------------------------------------------
dnl - Check OS
dnl ---------------------------------------------------------------------------

os_solaris=no
case "$host" in
   *solaris*) os_solaris=yes
   ;;
esac
AM_CONDITIONAL(OS_SOLARIS, test x$os_solaris = xyes)

dnl ---------------------------------------------------------------------------
dnl - Check for TCP Wrappers for XDMCP access control
dnl ---------------------------------------------------------------------------

if test x$with_tcp_wrappers = xno ; then
	echo "TCP wrappers disabled"
else
  LIBWRAP_PATH=""
  if test x$os_solaris = xyes ; then
	if test -f /usr/lib/libwrap.so; then
		LIBWRAP_PATH=/usr/lib/libwrap.so
		echo "Found $LIBWRAP_PATH" 1>&5
		nm $LIBWRAP_PATH | grep 'T setenv' && LIBWRAP_PATH=""
        fi

	if test -z "$LIBWRAP_PATH"; then
		if test -f /usr/sfw/lib/libwrap.so; then
			LIBWRAP_PATH=/usr/sfw/lib/libwrap.so
			echo "Found $LIBWRAP_PATH" 1>&5
			nm $LIBWRAP_PATH | grep 'T setenv' && LIBWRAP_PATH=""
        	fi
        fi

	if test -z "$LIBWRAP_PATH"; then
		echo "*********************************************************"
	        echo " You have a broken TCP wrappers library (setenv included)"
		echo " Please get the latest TCP wrappers package from your OS"
		echo " vendor, or recompile TCP wrappers to not include a"
		echo " setenv() implementation."
		echo
		echo "Not using TCP wrappers after all."
		echo "*********************************************************"
	else
		LIBWRAP_LIBS="-L/usr/sfw/lib -R/usr/sfw/lib -lwrap"
		AC_DEFINE(HAVE_TCPWRAPPERS, 1, [Define if have tcp wrappers])
	fi
  else
 	AC_CHECK_LIB([wrap], [hosts_ctl], [
 	LIBWRAP_LIBS="-lwrap"
 	LIBWRAP_PATH=auto
 	AC_DEFINE(HAVE_TCPWRAPPERS, 1, [Define if have tcp wrappers])])
  fi
 
  AC_MSG_CHECKING([whether to use TCP wrappers])
  if test -n "$LIBWRAP_PATH"; then
        AC_MSG_RESULT(yes)
  else
        AC_MSG_RESULT(no)
  fi

  dnl find out if we need -lnsl or whatever
  LIB_NSL=
  if test -n "$LIBWRAP_PATH"; then
		AC_MSG_CHECKING(whether -lwrap also requires -lnsl)
		ORIG_LIBS="$LIBS"
		LIBS="$EXTRA_DAEMON_LIBS $LIBS $LIBWRAP_LIBS"
		AC_TRY_LINK([
#include <tcpd.h>
int allow_severity, deny_severity;
], [return hosts_access;], ,[
dnl try with -lnsl
OLD_LIBS="$LIBS"
LIBS="$LIBS -lnsl"
AC_TRY_LINK([
#include <tcpd.h>
int allow_severity, deny_severity;
], [return hosts_access;], LIB_NSL="-lnsl",
LIBWRAP_PATH="")
LIBS="$OLD_LIBS"
])
		LIBS="$ORIG_LIBS"
		if test -n "$LIB_NSL"; then
			AC_MSG_RESULT(yes)
			EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS $LIB_NSL"
		else
			AC_MSG_RESULT(no)
		fi
  fi
fi

AC_SUBST(LIBWRAP_LIBS)


dnl ---------------------------------------------------------------------------
dnl - Check for XFT2 (for gdm-settings-daemon)
dnl ---------------------------------------------------------------------------

if $PKG_CONFIG --exists xft ; then
  AC_DEFINE(HAVE_XFT2,,[Define if Xft functionality is available])
fi


dnl ---------------------------------------------------------------------------
dnl - Check for Xinput
dnl ---------------------------------------------------------------------------

have_xinput=
AC_CHECK_LIB(Xi, XOpenDevice, XINPUT_LIBS=-lXi)
if test "x$XINPUT_LIBS" = x; then
  save_LIBS="$LIBS"
  for xinputpath in $x_libraries /usr/X11R6/lib /usr/openwin/lib; do
    LIBS="-L$xinputpath -lXi"
    if test x$os_solaris = xyes ; then
       XINPUT_RPATH_FLAGS="-R$xinputpath"
    fi
    AC_MSG_CHECKING(for -lXi in $xinputpath)
    AC_TRY_LINK([], [XOpenDevice()], [
	AC_MSG_RESULT(yes)
	XINPUT_LIBS="$XINPUT_RPATH_FLAGS -L$xinputpath -lXi"
        LIBS="$save_LIBS"
	break],[AC_MSG_RESULT(no)])
  done
  if test "x$XINPUT_LIBS" = x; then
    AC_MSG_ERROR(Couldn't find the XInput library. Check config.log for details)
  fi
fi
AC_CHECK_HEADER(X11/extensions/XInput.h, have_xinput=yes)
if test "x$have_xinput" = xyes; then
   AC_DEFINE(HAVE_XINPUT, 1, [Define if have xinput])
   X_EXTRA_LIBS="$X_EXTRA_LIBS $XINPUT_LIBS"
fi


dnl ---------------------------------------------------------------------------
dnl - Check for Xinerama
dnl ---------------------------------------------------------------------------

# X11 Xinerama extension
# Check for Xinerama extension (Solaris impl or Xfree impl)
xinerama_save_cppflags="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $X_CFLAGS"

ALL_X_LIBS="$X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"

if test ! x$with_xinerama = xno ; then
  # Check for XFree
  use_xfree_xinerama=yes
  AC_CHECK_LIB(Xinerama, XineramaQueryExtension,
         [AC_CHECK_HEADER(X11/extensions/Xinerama.h,
                       if test -z "`echo $ALL_X_LIBS | grep "\-lXext" 2> /dev/null`"; then
                          X_EXTRA_LIBS="-lXext $X_EXTRA_LIBS"
                       fi
                       AC_DEFINE(HAVE_XFREE_XINERAMA, 1, [Define if have xfree xinerama])
                       AC_DEFINE(HAVE_XINERAMA, 1, [Define if have xinerama])
                       XINERAMA_LIBS="-lXinerama"
                       XINERAMA_SUPPORT=yes,
                       use_xfree_xinerama=no,
                       [#include <X11/Xlib.h>])],
         use_xfree_xinerama=no, -lXext $ALL_X_LIBS)
  AC_MSG_CHECKING(for Xinerama support on XFree86)
  AC_MSG_RESULT($use_xfree_xinerama);

  if test x$use_xfree_xinerama = xno ; then
    if test x$os_solaris = xyes ; then
	# Check for solaris
	use_solaris_xinerama=yes
	AC_CHECK_LIB(Xext, XineramaGetInfo,
                     use_solaris_xinerama=yes, use_solaris_xinerama=no,
                     $ALL_X_LIBS)
	if test "x$use_solaris_xinerama" = "xyes"; then
            AC_CHECK_HEADER(X11/extensions/xinerama.h,
                            if test -z "`echo $ALL_X_LIBS | grep "\-lXext" 2> /dev/null`"; then
 				X_EXTRA_LIBS="-lXext $X_EXTRA_LIBS"
                            fi
                            AC_DEFINE(HAVE_SOLARIS_XINERAMA, 1, [Define if have Solaris xinerama])
                            AC_DEFINE(HAVE_XINERAMA, 1, [Define if have xinerama])
		     	    XINERAMA_LIBS=""
		     	    XINERAMA_SUPPORT=yes,
                            use_solaris_xinerama=no,
                            [#include <X11/Xlib.h>])
	fi
        AC_MSG_CHECKING(for Xinerama support on Solaris)
	AC_MSG_RESULT($use_solaris_xinerama);
    fi
  fi
fi

AC_SUBST(XINERAMA_LIBS)
CPPFLAGS="$xinerama_save_cppflags"

dnl ---------------------------------------------------------------------------
dnl - Check for systemd support
dnl ---------------------------------------------------------------------------

PKG_CHECK_MODULES(SYSTEMD,
                  [libsystemd])

AC_SUBST(SYSTEMD_CFLAGS)
AC_SUBST(SYSTEMD_LIBS)

PKG_CHECK_MODULES(JOURNALD,
                  [libsystemd],
                  [have_journald=yes], [have_journald=no])

if test "x$enable_systemd_journal" = "xauto" ; then
        if test x$have_journald = xno ; then
                use_journald=no
        else
                use_journald=yes
        fi

else
        use_journald="$enable_systemd_journal"
fi

if test "x$use_journald" != "xno" ; then
        if test "x$have_journald" = "xno"; then
                AC_MSG_ERROR([journald support explicitly required, but journald not found])
        fi

        AC_DEFINE(ENABLE_SYSTEMD_JOURNAL, 1, [Define to enable systemd journal support])
fi
AC_SUBST(JOURNALD_CFLAGS)
AC_SUBST(JOURNALD_LIBS)

if test "x$enable_wayland_support" != "xno" ; then
        AC_DEFINE(ENABLE_WAYLAND_SUPPORT, 1, [Define to enable wayland support])
fi

AC_PATH_PROG(SYSTEMD_X_SERVER, systemd-multi-seat-x, [/lib/systemd/systemd-multi-seat-x], [/lib/systemd:/usr/lib/systemd:$PATH])
AC_SUBST(SYSTEMD_X_SERVER)
AC_DEFINE_UNQUOTED(SYSTEMD_X_SERVER,"$SYSTEMD_X_SERVER",[Path to systemd X server wrapper])

if test "x$with_systemdsystemunitdir" != xno; then
        AC_SUBST(SYSTEMD_SYSTEM_UNIT_DIR, [$with_systemdsystemunitdir])
fi

AM_CONDITIONAL(INSTALL_SYSTEMD_UNITS, test x$with_systemdsystemunitdir != xno)

dnl ---------------------------------------------------------------------------
dnl - Check for plymouth support
dnl ---------------------------------------------------------------------------
PKG_CHECK_MODULES(PLYMOUTH,
                  [ply-boot-client],
                  [have_plymouth=yes], [have_plymouth=no])

if test "x$with_plymouth" = "xauto" ; then
        if test x$have_plymouth = xno ; then
                use_plymouth=no
        else
                use_plymouth=yes
        fi
else
        use_plymouth="$with_plymouth"
fi

PLYMOUTH_QUIT_SERVICE=""
if test "x$use_plymouth" != "xno" ; then
        if test "x$have_plymouth" = "xno"; then
                AC_MSG_ERROR([Plymouth support explicitly required, but plymouth not found])
        fi

        AC_DEFINE(WITH_PLYMOUTH, 1, [Define to enable plymouth support])
        PLYMOUTH_QUIT_SERVICE="plymouth-quit.service"
fi
AC_SUBST(PLYMOUTH_CFLAGS)
AC_SUBST(PLYMOUTH_LIBS)
AC_SUBST(PLYMOUTH_QUIT_SERVICE)

dnl ---------------------------------------------------------------------------
dnl - Check for D-Bus
dnl ---------------------------------------------------------------------------

dnl - Are we specifying a different dbus root ?
AC_ARG_WITH(dbus-sys,
            AS_HELP_STRING([--with-dbus-sys=<dir>],
	                   [where D-BUS system.d directory is]))
AC_ARG_WITH(dbus-services,
            AS_HELP_STRING([--with-dbus-services=<dir>],
	                   [where D-BUS services directory is]))

if ! test -z "$with_dbus_sys" ; then
        DBUS_SYS_DIR="$with_dbus_sys"
else
        DBUS_SYS_DIR="$sysconfdir/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

dnl ---------------------------------------------------------------------------
dnl - Check for ISO Codes
dnl ---------------------------------------------------------------------------

AC_MSG_CHECKING([whether iso-codes has iso-639 domain])
if $PKG_CONFIG --variable=domains iso-codes | grep 639 >/dev/null ; then
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi
AC_DEFINE_UNQUOTED([ISO_CODES_PREFIX],["`$PKG_CONFIG --variable=prefix iso-codes`"],[ISO codes prefix])
ISO_CODES=iso-codes


dnl ---------------------------------------------------------------------------
dnl check for RBAC
dnl ---------------------------------------------------------------------------

msg_rbac_shutdown=no
AC_ARG_ENABLE(rbac-shutdown,
              AC_HELP_STRING([--enable-rbac-shutdown=<key>],
	                     [Build with RBAC support specifying shutdown/reboot RBAC authentication key]),
              enable_rbac_shutdown=$enableval,enable_rbac_shutdown=no)

if test "x$enable_rbac_shutdown" != "xno"; then
        RBAC_LIBS="-lsecdb -lsocket -lnsl"
        AC_DEFINE(ENABLE_RBAC_SHUTDOWN, [], [Set if we build with RBAC support])
        AC_DEFINE_UNQUOTED(RBAC_SHUTDOWN_KEY, "$enable_rbac_shutdown", [Set if we build with RBAC support])
        msg_rbac_shutdown="yes, using key $enable_rbac_shutdown"
fi
AC_SUBST(RBAC_LIBS)


dnl ---------------------------------------------------------------------------
dnl check for backtrace support
dnl ---------------------------------------------------------------------------

AC_CHECK_HEADERS(execinfo.h)

dnl ---------------------------------------------------------------------------
dnl check for process control
dnl ---------------------------------------------------------------------------

AC_CHECK_HEADERS(sys/prctl.h)

dnl ---------------------------------------------------------------------------
dnl - Define some variables to represent the directories we use.
dnl ---------------------------------------------------------------------------

AC_SUBST(gdmlocaledir, ${gdmconfdir})
AC_SUBST(pixmapdir, ${datadir}/pixmaps)

AC_ARG_WITH(log-dir,
            AS_HELP_STRING([--with-log-dir=<file>],
                           [log dir]))

if ! test -z "$with_log_dir"; then
   GDM_LOG_DIR=$with_log_dir
else
   GDM_LOG_DIR=/var/log/gdm
fi
AC_SUBST(logdir, $GDM_LOG_DIR)

withval=""
AC_ARG_WITH(at-bindir,
            AS_HELP_STRING([--with-at-bindir=<PATH>]
                           [PATH to Accessible Technology programs @<:@default=BINDIR@:>@]))

if test x$withval != x; then
	AT_BINDIR="$withval"
else
	AT_BINDIR='${bindir}'
fi

AC_SUBST(AT_BINDIR)

withval=""
AC_ARG_WITH(defaults_conf,
            AS_HELP_STRING([--with-defaults-conf=<FILENAME>],
                           [FILENAME to give to defaults file @<:@default=DATADIR/gdm/defaults.conf@:>@]))

if test x$withval != x; then
	GDM_DEFAULTS_CONF="$withval"
else
	GDM_DEFAULTS_CONF='${datadir}/gdm/defaults.conf'
fi

AC_SUBST(GDM_DEFAULTS_CONF)

withval=""
AC_ARG_WITH(custom_conf,
            AS_HELP_STRING([--with-custom-conf=<FILENAME>],
                           [FILENAME to give to custom configuration file @<:@default=GDMCONFDIR/custom.conf@:>@]))

if test x$withval != x; then
	GDM_CUSTOM_CONF="$withval"
else
	GDM_CUSTOM_CONF='${gdmconfdir}/custom.conf'
fi

AC_SUBST(GDM_CUSTOM_CONF)
AC_SUBST(GDM_OLD_CONF, '${gdmconfdir}/gdm.conf')

AC_ARG_WITH(gnome-settings-daemon-directory,
              [AC_HELP_STRING([--with-gnome-settings-daemon-directory],
                              [Specify the directory of gnome-settings-daemon used by the chooser @<:@default=libexecdir@:>@])],,
                              [with_gnome_settings_daemon_directory="\${libexecdir}"])

GNOME_SETTINGS_DAEMON_DIR=$with_gnome_settings_daemon_directory
AC_SUBST(GNOME_SETTINGS_DAEMON_DIR)

AC_ARG_WITH(check-accelerated-directory,
              [AC_HELP_STRING([--with-check-accelerated-directory],
                              [Specify the directory of gnome-session-check-accelerated @<:@default=libexecdir@:>@])],,
                              [with_check_accelerated_directory="\${libexecdir}"])

CHECK_ACCELERATED_DIR=$with_check_accelerated_directory
AC_SUBST(CHECK_ACCELERATED_DIR)

dnl ---------------------------------------------------------------------------
dnl - Check for XEvIE extension support
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(xevie,
            AS_HELP_STRING([--with-xevie],
                           [Add XEvIE Xserver extension support @<:@default=no@:>@]),,
            with_xevie=no)

if test x$with_xevie != xno ; then
	XEVIE_OPTION="+extension XEVIE"
else
	XEVIE_OPTION=""
fi

AC_SUBST(XEVIE_OPTION)
AC_DEFINE_UNQUOTED(XEVIE_OPTION,"$XEVIE_OPTION",[Define xevie option])

dnl ---------------------------------------------------------------------------
dnl - Check for audit framework
dnl ---------------------------------------------------------------------------

# Check for Solaris auditing API
# Note, Solaris auditing not supported for Solaris 9 or earlier and
# should not be used on these versions of Solaris if auditing is
# required.  Solaris auditing is only supported on systems that
# support the ADT_USER enumeration value.
#
AC_MSG_CHECKING(for Solaris auditing API)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <bsm/adt.h>
adt_user_context = ADT_USER;
]], [[]])], [ check_sun_audit=yes ], [ check_sun_audit=no ])

if test ${check_sun_audit} = yes
then
   AC_DEFINE(HAVE_ADT, 1, [Define if have adt])
   PAM_LIBS="$PAM_LIBS -lbsm"
   AC_MSG_RESULT(yes)
else
   AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(HAVE_ADT, test x$check_sun_audit = xyes)

# Check for Linux auditing API
#
AC_ARG_WITH(libaudit,
            AS_HELP_STRING([--with-libaudit],
                           [Add Linux audit support @<:@default=auto@:>@]),,
            with_libaudit=auto)

# libaudit detection
if test x$with_libaudit = xno ; then
    have_libaudit=no;
else
    # See if we have audit daemon library
    AC_CHECK_LIB(audit, audit_log_user_message,
                 have_libaudit=yes, have_libaudit=no)
fi

AM_CONDITIONAL(HAVE_LIBAUDIT, test x$have_libaudit = xyes)

if test x$have_libaudit = xyes ; then
    PAM_LIBS="$PAM_LIBS -laudit"
    AC_DEFINE(HAVE_LIBAUDIT,1,[linux audit support])
fi

#
# Subst the extra libs
#
AC_SUBST(EXTRA_DAEMON_LIBS)
AC_SUBST(EXTRA_CHOOSER_LIBS)
AC_SUBST(EXTRA_XNEST_LIBS)
AC_SUBST(EXTRA_FLEXI_LIBS)
AC_SUBST(EXTRA_DYNAMIC_LIBS)
AC_SUBST(EXTRA_SETUP_LIBS)
AC_SUBST(EXTRA_TEST_LIBS)

# Check for Solaris logindevperm support
#
AC_CHECK_LIB(devinfo, di_devperm_login, [
                        AC_DEFINE(HAVE_LOGINDEVPERM)
                            PAM_LIBS="$PAM_LIBS -ldevinfo" ])

dnl ---------------------------------------------------------------------------
dnl - Check for X Server location
dnl ---------------------------------------------------------------------------

# First check with "! -h" for /usr/X11R6 and /usr/X11 since they often
# symlink to each other, and configure should use the more stable
# location (the real directory) if possible.
#
# On Solaris, the /usr/bin/Xserver script is used to decide whether to
# use Xsun or Xorg, so this is used on Solaris.
#
# When testing for /usr/X11R6, first check with "! -h" for /usr/X11R6
# and /usr/X11 since they often symlink to each other, and configure
# should use the more stable location (the real directory) if possible.
#
if test -x /usr/bin/X; then
   X_PATH="/usr/bin"
   X_SERVER_PATH="/usr/bin"
   X_SERVER="/usr/bin/X"
elif test -x /usr/X11/bin/Xserver; then
   X_PATH="/usr/X11/bin"
   X_SERVER_PATH="/usr/X11/bin"
   X_SERVER="/usr/X11/bin/Xserver"
elif test ! -h /usr/X11R6 -a -x /usr/X11R6/bin/X; then
   X_PATH="/usr/X11R6/bin"
   X_SERVER_PATH="/usr/X11R6/bin"
   X_SERVER="/usr/X11R6/bin/X"
elif test ! -h /usr/X11 -a -x /usr/X11/bin/X; then
   X_PATH="/usr/X11/bin"
   X_SERVER_PATH="/usr/X11/bin"
   X_SERVER="/usr/X11/bin/X"
elif test -x /usr/X11R6/bin/X; then
   X_PATH="/usr/X11R6/bin"
   X_SERVER_PATH="/usr/X11R6/bin"
   X_SERVER="/usr/X11R6/bin/X"
elif test -x /usr/bin/Xorg; then
   X_PATH="/usr/bin"
   X_SERVER_PATH="/usr/bin"
   X_SERVER="/usr/bin/Xorg"
elif test -x /usr/X11/bin/X; then
   X_PATH="/usr/X11/bin"
   X_SERVER_PATH="/usr/X11/bin"
   X_SERVER="/usr/X11/bin/X"
elif test -x /usr/openwin/bin/Xsun; then
   # Do not add /usr/openwin/bin here because on Solaris you need
   # /usr/openwin/bin in your PATH even if you are using the Xorg
   # Xserver.  We add this to the path below.
   X_PATH="/usr/openwin/bin"
   X_SERVER_PATH="/usr/openwin/bin"
   X_SERVER="/usr/openwin/bin/Xsun"
elif test -x /opt/X11R6/bin/X; then
   X_PATH="/opt/X11R6/bin"
   X_SERVER_PATH="/opt/X11R6/bin"
   X_SERVER="/opt/X11R6/bin/X"
else
   # what to do, what to do, this is wrong, but this just sets the
   # defaults, perhaps this user is cross compiling or some such
   X_PATH="/usr/bin/X11:/usr/X11R6/bin:/opt/X11R6/bin"
   X_SERVER_PATH="/usr/bin"
   X_SERVER="/usr/bin/X"
fi

dnl ---------------------------------------------------------------------------
dnl - Check if Xorg is new enough to require '-listen tcp' (1.17)
dnl ---------------------------------------------------------------------------

if $PKG_CONFIG --atleast-version=1.17 xorg-server; then
   AC_DEFINE([HAVE_XSERVER_THAT_DEFAULTS_TO_LOCAL_ONLY], [], [XServer disables tcp access by default])
fi

dnl ---------------------------------------------------------------------------
dnl - Check for Xnest / Xephyr support
dnl ---------------------------------------------------------------------------

# Use Xephyr if it is available.  It works better than Xnest since Xephyr
# supports the Xserver extensions, even if on a remote machine.
#
X_XNEST_UNSCALED_FONTPATH="true"
if test -x $X_SERVER_PATH/Xephyr; then
    X_XNEST_CMD="$X_SERVER_PATH/Xephyr"
    X_XNEST_CONFIG_OPTIONS="-audit 0"
else
    if test -x /usr/openwin/bin/Xnest; then
        # If on Solaris, Xnest is only shipped with the Xsun Xserver, so
        # use this version.
        #
        X_XNEST_CMD="/usr/openwin/bin/Xnest"
        X_XNEST_CONFIG_OPTIONS="-audit 0 -name Xnest -pn"
        X_XNEST_UNSCALED_FONTPATH="false"
    else
        if test -x $X_SERVER_PATH/Xnest; then
            X_XNEST_CMD="$X_SERVER_PATH/Xnest"
            X_XNEST_CONFIG_OPTIONS="-audit 0 -name Xnest"
        fi
    fi
fi

dnl ---------------------------------------------------------------------------
dnl - Expand vars
dnl ---------------------------------------------------------------------------

AS_AC_EXPAND(LOCALSTATEDIR, $localstatedir)
AS_AC_EXPAND(SYSCONFDIR, $sysconfdir)
AS_AC_EXPAND(DATADIR, $datadir)
AS_AC_EXPAND(BINDIR, $bindir)
AS_AC_EXPAND(SBINDIR, $sbindir)
AS_AC_EXPAND(LIBDIR, $libdir)
AS_AC_EXPAND(LIBEXECDIR, $libexecdir)

dnl ---------------------------------------------------------------------------
dnl - runtime directory
dnl ---------------------------------------------------------------------------

AC_ARG_WITH([run-dir],
            AS_HELP_STRING([--with-run-dir=<file>],
                           [runtime directory]))

if ! test -z "$with_run_dir"; then
   GDM_RUN_DIR=$with_run_dir
else
   GDM_RUN_DIR=${localstatedir}/run/gdm
fi

AC_SUBST([GDM_RUN_DIR])

dnl ---------------------------------------------------------------------------
dnl - PID file
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(pid-file,
            AS_HELP_STRING([--with-pid-file=<file>],
                           [pid file]))

if ! test -z "$with_pid_file"; then
   GDM_PID_FILE=$with_pid_file
else
   GDM_PID_FILE=$GDM_RUN_DIR/gdm.pid
fi

AS_AC_EXPAND(GDM_PID_FILE, "$GDM_PID_FILE")
AC_DEFINE_UNQUOTED(GDM_PID_FILE, "$GDM_PID_FILE", [pid file])

dnl ---------------------------------------------------------------------------
dnl - ran once marker
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(ran-once-marker-directory,
            AS_HELP_STRING([--with-ran-once-marker-directory=<dir>],
                           [ran once marker directory]))

if ! test -z "$with_ran_once_marker_directory"; then
   GDM_RAN_ONCE_MARKER_DIR=$with_ran_once_marker_directory
else
   GDM_RAN_ONCE_MARKER_DIR=$GDM_RUN_DIR
fi
AC_SUBST(GDM_RAN_ONCE_MARKER_DIR)
AS_AC_EXPAND(GDM_RAN_ONCE_MARKER_DIR_EXPANDED, $GDM_RAN_ONCE_MARKER_DIR)
AC_DEFINE_UNQUOTED(GDM_RAN_ONCE_MARKER_DIR, "$GDM_RAN_ONCE_MARKER_DIR_EXPANDED", [ran once marker dir])

GDM_RAN_ONCE_MARKER_FILE="$GDM_RAN_ONCE_MARKER_DIR/ran-once-marker"
AC_SUBST(GDM_RAN_ONCE_MARKER_FILE)
AS_AC_EXPAND(GDM_RAN_ONCE_MARKER_FILE_EXPANDED, $GDM_RAN_ONCE_MARKER_FILE)
AC_DEFINE_UNQUOTED(GDM_RAN_ONCE_MARKER_FILE, "$GDM_RAN_ONCE_MARKER_FILE_EXPANDED", [ran once marker file])

dnl ---------------------------------------------------------------------------
dnl - GREETER WORKING DIRECTORY
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(working-directory,
            AS_HELP_STRING([--with-working-dir=<dir>],
                           [working directory]))

if ! test -z "$with_working_directory"; then
   GDM_WORKING_DIR=$with_working_directory
else
   GDM_WORKING_DIR=${localstatedir}/lib/gdm
fi

AC_SUBST(GDM_WORKING_DIR)

dnl ---------------------------------------------------------------------------
dnl - Directory for X auth cookies
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(xauth-dir,
            AS_HELP_STRING([--with-xauth-dir=<dir>],
                           [xauth cookie directory]))

if ! test -z "$with_xauth_dir"; then
   GDM_XAUTH_DIR=$with_xauth_dir
else
   GDM_XAUTH_DIR=$GDM_RUN_DIR
fi

AC_SUBST(GDM_XAUTH_DIR)

dnl ---------------------------------------------------------------------------
dnl - Directory for greeter screenshot
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(screenshot-dir,
            AS_HELP_STRING([--with-screenshot-dir=<dir>],
                           [directory to store greeter screenshot]))

if ! test -z "$with_screenshot_dir"; then
   GDM_SCREENSHOT_DIR=$with_screenshot_dir
else
   GDM_SCREENSHOT_DIR=$GDM_RUN_DIR/greeter
fi

AC_SUBST(GDM_SCREENSHOT_DIR)

dnl ---------------------------------------------------------------------------
dnl - Finish
dnl ---------------------------------------------------------------------------


# Turn on the additional warnings last, so -Werror doesn't affect other tests.

AC_ARG_ENABLE(more-warnings,
              AS_HELP_STRING([--enable-more-warnings],
                             [Maximum compiler warnings]),
              set_more_warnings="$enableval",[
	      if test -d $srcdir/.git; then
	        set_more_warnings=yes
	      else
	        set_more_warnings=no
              fi])

AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
        AC_MSG_RESULT(yes)
        CFLAGS="\
        -Wall \
        -Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
        -Wnested-externs -Wpointer-arith \
        -Wcast-align -Wsign-compare \
        $CFLAGS"

        for option in -Wno-strict-aliasing -Wno-sign-compare; do
                SAVE_CFLAGS="$CFLAGS"
                CFLAGS="$CFLAGS $option"
                AC_MSG_CHECKING([whether gcc understands $option])
                AC_TRY_COMPILE([], [],
                        has_option=yes,
                        has_option=no,)
                if test $has_option = no; then
                        CFLAGS="$SAVE_CFLAGS"
                fi
                AC_MSG_RESULT($has_option)
                unset has_option
                unset SAVE_CFLAGS
        done
        unset option
else
        AC_MSG_RESULT(no)
fi

#
# Enable Debug
#
AC_ARG_ENABLE(debug,
              AS_HELP_STRING([--enable-debug],
                             [turn on debugging]),,
              enable_debug=yes)

if test "$enable_debug" = "yes"; then
	DEBUG_CFLAGS="-DG_ENABLE_DEBUG"
else
	if test "x$enable_debug" = "xno"; then
		DEBUG_CFLAGS="-DG_DISABLE_ASSERT -DG_DISABLE_CHECKS"
  	else
    		DEBUG_CFLAGS=""
  	fi
fi
AC_SUBST(DEBUG_CFLAGS)

#
# Enable Profiling
#
AC_ARG_ENABLE(profiling,
              AS_HELP_STRING([--enable-profiling],
                             [turn on profiling]),,
              enable_profiling=yes)

if test "$enable_profiling" = "yes"; then
    AC_DEFINE(ENABLE_PROFILING,1,[enable profiling])
fi

#
# Set SHELL to use in scripts.
#
if test x$os_solaris = xyes ; then
   XSESSION_SHELL=/bin/ksh
else
   XSESSION_SHELL=/bin/sh
fi

#
# Set VT to use for initial server
#
AC_ARG_WITH(initial-vt,
        AS_HELP_STRING([--with-initial-vt=<nr>],
                [Initial virtual terminal to use]))
if ! test -z "$with_initial_vt"; then
        GDM_INITIAL_VT="$with_initial_vt"
else
        GDM_INITIAL_VT="1"
fi
AC_SUBST(GDM_INITIAL_VT)
AC_DEFINE_UNQUOTED(GDM_INITIAL_VT, "$GDM_INITIAL_VT", [Initial Virtual Terminal])

# Set configuration choices.
#
AC_SUBST(XSESSION_SHELL)
AC_DEFINE_UNQUOTED(XSESSION_SHELL,"$XSESSION_SHELL",[xsession shell])
AC_SUBST(SOUND_PROGRAM)
AC_DEFINE_UNQUOTED(SOUND_PROGRAM,"$SOUND_PROGRAM",[])

AC_SUBST(X_PATH)
AC_SUBST(X_SERVER)
AC_SUBST(X_SERVER_PATH)
AC_DEFINE_UNQUOTED(X_SERVER,"$X_SERVER",[])
AC_DEFINE_UNQUOTED(X_SERVER_PATH,"$X_SERVER_PATH",[])

AC_SUBST(X_XNEST_CMD)
AC_SUBST(X_XNEST_CONFIG_OPTIONS)
AC_SUBST(X_XNEST_UNSCALED_FONTPATH)
AC_DEFINE_UNQUOTED(X_XNEST_CMD,"$X_XNEST_CMD",[])
AC_DEFINE_UNQUOTED(X_XNEST_CONFIG_OPTIONS,"$X_XNEST_CONFIG_OPTIONS",[])
AC_DEFINE_UNQUOTED(X_XNEST_UNSCALED_FONTPATH,"$X_XNEST_UNSCALED_FONTPATH",[])

## Stuff for debian/changelog.in
#if test -e "debian/changelog"; then
#  DEBIAN_DATESTAMP=`head -1 debian/changelog| sed -e 's/.*cvs.//' -e 's/).*//'`
#  DEBIAN_DATE=`grep '^ --' debian/changelog | head -1 | sed -e 's/.*  //'`
#else
#  DEBIAN_DATESTAMP=`date +%Y%m%d%H%M%s`
#  DEBIAN_DATE=`date -R`
#fi
#
#AC_SUBST(DEBIAN_DATESTAMP)
#AC_SUBST(DEBIAN_DATE)

AC_CONFIG_FILES([
Makefile
pam-extensions/Makefile
pam-extensions/gdm-pam-extensions.pc
daemon/Makefile
docs/Makefile
chooser/Makefile
libgdm/Makefile
libgdm/gdm.pc
utils/Makefile
pam_gdm/Makefile
data/gdm.conf
data/Makefile
data/applications/Makefile
data/autostart/Makefile
data/pixmaps/Makefile
data/pixmaps/16x16/Makefile
data/pixmaps/32x32/Makefile
data/pixmaps/48x48/Makefile
data/dconf/gdm
common/Makefile
po/Makefile.in
tests/Makefile
])

AC_OUTPUT

dnl ---------------------------------------------------------------------------
dnl - Show summary
dnl ---------------------------------------------------------------------------

echo "
                    GDM $VERSION
                    ============

	prefix:                   ${prefix}
	exec_prefix:              ${exec_prefix}
        libdir:                   ${libdir}
        bindir:                   ${bindir}
        sbindir:                  ${sbindir}
        sysconfdir:               ${sysconfdir}
        sysconfsubdir:            ${sysconfsubdir}
        gdmconfdir:               ${gdmconfdir}
        dmconfdir:                ${dmconfdir}
        localstatedir:            ${localstatedir}
        datadir:                  ${datadir}
	gnome-settings-daemon location: ${with_gnome_settings_daemon_directory}
	gnome-session-check-accel location: ${with_check_accelerated_directory}
	source code location:	  ${srcdir}
	compiler:		  ${CC}
	cflags:		          ${CFLAGS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}

        user:                     ${GDM_USERNAME}
        group:                    ${GDM_GROUPNAME}

        dbus-1 system.d dir:      ${DBUS_SYS_DIR}
        PAM prefix:               ${PAM_PREFIX}
        PAM module dir:           ${PAM_MOD_DIR}
        PAM config:               ${with_default_pam_config}
        X server:                 ${X_SERVER}
"

dnl TCP Wrappers support?
if test x"$LIBWRAP_PATH" = x ; then
	echo \
"        TCP Wrappers support:     no"
else
	echo \
"        TCP Wrappers support:     yes"
fi

echo \
"        Xinerama support:         ${XINERAMA_SUPPORT}
        XDMCP support:            ${XDMCP_SUPPORT}
        SELinux support:          ${use_selinux}
        systemd unit dir:         ${with_systemdsystemunitdir}
        plymouth support:         ${use_plymouth}
        wayland support:          ${enable_wayland_support}
        Build with RBAC:          ${msg_rbac_shutdown}
        Initial VT:               ${GDM_INITIAL_VT}
        Enable documentation:     ${enable_documentation}
        Install GDM's Xsession:   ${enable_gdm_xsession}
"
