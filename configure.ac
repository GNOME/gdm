AC_PREREQ(2.52)
AC_INIT(daemon/gdm.h)

AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(gdm,2.19.2)
AC_CONFIG_MACRO_DIR([m4])
AM_MAINTAINER_MODE

IT_PROG_INTLTOOL([0.35.0])

GNOME_DOC_INIT

DBUS_REQUIRED=0.30
GLIB_REQUIRED=2.8.0
GTK_REQUIRED=2.6.0
PANGO_REQUIRED=1.3.0
LIBGLADE_REQUIRED=1.99.2
LIBGNOMECANVAS_REQUIRED=1.109.0
LIBRSVG_REQUIRED=1.1.1
LIBXML_REQUIRED=2.4.12
LIBART_REQUIRED=2.3.11
SCROLLKEEPER_REQUIRED=0.1.4

dnl
dnl Let the user configure where to look for the configuration files.
dnl

AC_ARG_WITH(sysconfsubdir, AC_HELP_STRING([--with-sysconfsubdir],
            [directory name used under sysconfdir, default=gdm]),
            sysconfsubdir=${withval}, sysconfsubdir=gdm)
AC_SUBST(sysconfsubdir)

if test x"${sysconfsubdir}" != xno -a x"${sysconfsubdir}" != x; then
    gdmconfdir='${sysconfdir}/${sysconfsubdir}'
else
    gdmconfdir='${sysconfdir}'
fi
AC_SUBST(gdmconfdir)

AC_ARG_WITH(dmconfdir, AC_HELP_STRING([--with-dmconfdir],
            [directory where Sessions are stored, default=SYSCONFDIR/dm]),
            dmconfdir=${withval}, dmconfdir=${sysconfdir}/dm)
AC_SUBST(dmconfdir)

dnl
dnl Configure arguments
dnl
AC_ARG_ENABLE(console-helper,
  [  --enable-console-helper=[auto/no/yes]  Enable PAM console helper [default=auto]],,
  enable_console_helper=auto)
    
AC_ARG_ENABLE(authentication-scheme,
  [  --enable-authentication-scheme=[auto/pam/crypt/shadow]  Choose a specific
                          authentication scheme [default=auto]],,
  enable_authentication_scheme=auto)

AC_ARG_WITH(xinerama,
  [  --with-xinerama=[auto/yes/no]  Add Xinerama support [default=auto]],,
  with_xinerama=auto)

AC_ARG_WITH(xdmcp,
  [  --with-xdmcp=[auto/yes/no]  Add XDMCP (remote login) support [default=auto]],,
  with_xdmcp=auto)

AC_ARG_WITH(tcp-wrappers,
  [  --with-tcp-wrappers=[auto/yes/no]  Use TCP Wrappers [default=auto]],,
  with_tcp_wrappers=auto)

AC_ARG_WITH(dmx,
  [  --with-dmx=[auto/yes/no]  Add DMX (Distributed Multihead X) support [default=auto]],,
  with_dmx=auto)

AC_ARG_WITH(selinux, [  --with-selinux  Add SELinux support])

AC_ARG_WITH(console-kit,
  [  --with-console-kit=[auto/yes/no]  Add ConsoleKit support [default=auto]],,
  with_console_kit=auto)

# Some distros may want to add directories to the end of the default path.
# The --with-post-path can be used to add these at configure time.
#
# On Solaris, you should use "--with-post-path=/usr/openwin/bin" for
# full Xserver support since some X programs are still in the older
# location (such as sessreg).  Someday the Sun Xserver team will fix this,
# and then you will not have to do this anymore.
#
withval=""
AC_ARG_WITH(post-path,
[  --with-post-path=<PATH>   add PATH to end of user's PATH when logging in],[
if test x$withval != x; then
   AC_MSG_RESULT("PATH ${withval} with be added to end of user's PATH when logging in.")
fi])
if test x$withval != x; then
	USER_POST_PATH="$withval"
fi

dnl
dnl PAM prefix
dnl
withval=""
AC_ARG_WITH(pam-prefix,
[  --with-pam-prefix=<prefix>   specify where pam files go],[
if test x$withval != x; then
   AC_MSG_RESULT("PAM files will be installed in prefix ${withval}.")
fi])
if test x$withval != x; then
	PAM_PREFIX="$withval"
else
	PAM_PREFIX='${sysconfdir}'
fi
AC_SUBST(PAM_PREFIX)

dnl
dnl file that sets LANG
dnl
withval=""
AC_ARG_WITH(lang-file,
    [  --with-lang-file=<filename>   file containing default language setting],[
if test x$withval != x; then
AC_MSG_RESULT("System locale will be looked for in file ${withval}.")
fi])
if test x$withval != x; then
	LANG_CONFIG_FILE="$withval"
else
	LANG_CONFIG_FILE='${sysconfdir}/sysconfig/i18n'
fi
AC_SUBST(LANG_CONFIG_FILE)
    
AC_PATH_PROG(CONSOLE_HELPER,consolehelper,no)
if test "x$CONSOLE_HELPER" = "xno" ; then
  if test "x$enable_console_helper" = "xyes" ; then
    AC_MSG_ERROR(Console helper requested but consolehelper binary not found)
  fi
  # if it was no, nothing happens, if it was auto, well then we're out of luck
  enable_console_helper=no
else
  if test ! "x$enable_console_helper" = "xno" ; then
    enable_console_helper=yes
  fi
fi

AC_ISC_POSIX
AC_PROG_CC
AC_STDC_HEADERS
dnl AC_ARG_PROGRAM
AM_PROG_LIBTOOL

AC_PATH_XTRA

dnl socklen_t may be declared, but not in a "standard" C header location
AC_CHECK_HEADERS(sys/socket.h)
AC_CHECK_TYPE(socklen_t,,
        AC_DEFINE(socklen_t,size_t,Compatibility type),
[AC_INCLUDES_DEFAULT]
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
)

AC_CHECK_FUNCS([setresuid setenv unsetenv clearenv])

dnl checks needed for Darwin compatibility to linux **environ.
AC_CHECK_HEADERS(crt_externs.h)
AC_CHECK_FUNCS(_NSGetEnviron)

GNOME_COMPILE_WARNINGS
CFLAGS="$CFLAGS $WARN_CFLAGS"

PKG_CHECK_MODULES(COMMON, gtk+-2.0 >= $GTK_REQUIRED libglade-2.0 >= $LIBGLADE_REQUIRED)
AC_SUBST(COMMON_CFLAGS)
AC_SUBST(COMMON_LIBS)

PKG_CHECK_MODULES(DAEMON, gtk+-2.0 >= $GTK_REQUIRED)
AC_SUBST(DAEMON_CFLAGS)
AC_SUBST(DAEMON_LIBS)

PKG_CHECK_MODULES(GUI, gtk+-2.0 >= $GTK_REQUIRED libglade-2.0 >= $LIBGLADE_REQUIRED)
AC_SUBST(GUI_CFLAGS)
AC_SUBST(GUI_LIBS)

PKG_CHECK_MODULES(GUIGLADE, gtk+-2.0 >= $GTK_REQUIRED libglade-2.0 >= $LIBGLADE_REQUIRED)
AC_SUBST(GUIGLADE_CFLAGS)
AC_SUBST(GUIGLADE_LIBS)

PKG_CHECK_MODULES(GUING, gtk+-2.0 >= $GTK_REQUIRED)
AC_SUBST(GUING_CFLAGS)
AC_SUBST(GUING_LIBS)

PKG_CHECK_MODULES(GREETER, gtk+-2.0 >= $GTK_REQUIRED libgnomecanvas-2.0 >= $LIBGNOMECANVAS_REQUIRED librsvg-2.0 >= $LIBRSVG_REQUIRED libxml-2.0 >= $LIBXML_REQUIRED libart-2.0 >= $LIBART_REQUIRED)
AC_SUBST(GREETER_CFLAGS)
AC_SUBST(GREETER_LIBS)

PKG_CHECK_MODULES(UTILS, gtk+-2.0 >= $GTK_REQUIRED libglade-2.0 >= $LIBGLADE_REQUIRED)
AC_SUBST(UTILS_CFLAGS)
AC_SUBST(UTILS_LIBS)

PKG_CHECK_MODULES(GLIB, glib-2.0 >= $GLIB_REQUIRED)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GOBJECT, gobject-2.0 >= $GLIB_REQUIRED)
AC_SUBST(GOBJECT_CFLAGS)
AC_SUBST(GOBJECT_LIBS)

PKG_CHECK_MODULES(PANGO, pango >= $PANGO_REQUIRED)
AC_SUBST(PANGO_CFLAGS)
AC_SUBST(PANGO_LIBS)

PKG_CHECK_MODULES(PANGOFT, pangoft2 >= $PANGO_REQUIRED)
AC_SUBST(PANGOFT_CFLAGS)
AC_SUBST(PANGOFT_LIBS)

PKG_CHECK_MODULES(GDK, gdk-2.0 >= $GTK_REQUIRED)
AC_SUBST(GDK_CFLAGS)
AC_SUBST(GDK_LIBS)

PKG_CHECK_MODULES(GDKPIXBUF, gdk-pixbuf-2.0 >= $GTK_REQUIRED)
AC_SUBST(GDKPIXBUF_CFLAGS)
AC_SUBST(GDKPIXBUF_LIBS)

dnl Allow users to run gdmsetup using the console helper PAM stuff.
if test "x$enable_console_helper" = "xyes"; then
  AM_CONDITIONAL(CONSOLE_HELPER, true)
else
  AM_CONDITIONAL(CONSOLE_HELPER, false)
fi

dnl ## Language Support
GETTEXT_PACKAGE=gdm
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE")
AC_SUBST(GETTEXT_PACKAGE)

dnl ## internationalization support
AM_GLIB_GNU_GETTEXT

EXTRA_DAEMON_LIBS=""
EXTRA_CHOOSER_LIBS=""
EXTRA_XNEST_LIBS=""
EXTRA_FLEXI_LIBS=""
EXTRA_DYNAMIC_LIBS=""
EXTRA_SETUP_LIBS=""
EXTRA_GREETER_LIBS=""

AC_CHECK_FUNC(socket,,[
	      AC_CHECK_LIB(socket,socket, [
			   EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lsocket"
			   EXTRA_CHOOSER_LIBS="$EXTRA_CHOOSER_LIBS -lsocket"
			   EXTRA_XNEST_LIBS="$EXTRA_XNEST_LIBS -lsocket"
			   EXTRA_FLEXI_LIBS="$EXTRA_FLEXI_LIBS -lsocket"
			   EXTRA_DYNAMIC_LIBS="$EXTRA_DYNAMIC_LIBS -lsocket"
			   EXTRA_SETUP_LIBS="$EXTRA_SETUP_LIBS -lsocket"])])
AC_CHECK_FUNC(gethostbyname,,[
	      AC_CHECK_LIB(nsl,gethostbyname, [
			   EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lnsl"
			   EXTRA_CHOOSER_LIBS="$EXTRA_CHOOSER_LIBS -lnsl"])])
AC_CHECK_FUNC(sched_yield,[
	      AC_DEFINE(HAVE_SCHED_YIELD)],[
	      AC_CHECK_LIB(rt,sched_yield, [
			   AC_DEFINE(HAVE_SCHED_YIELD)
			   EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lrt"], [
			   echo "No sched_yield found"])])
AC_CHECK_FUNC(inet_aton,,[
	      AC_CHECK_LIB(resolv,inet_aton, [
			   EXTRA_CHOOSER_LIBS="$EXTRA_CHOOSER_LIBS -lresolv"])])

COMMON_GNOME_LIBS="libgdmcommon-ui.a"
AC_SUBST(COMMON_GNOME_LIBS)

dnl *****************************
dnl     IPv6 specific checks
dnl *****************************
AC_MSG_CHECKING([whether to enable IPv6])
AC_ARG_ENABLE(ipv6, [  --enable-ipv6=[yes/no] Enables compilation of IPv6 code default=[no]],, enable_ipv6=no)
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
        AC_DEFINE(ENABLE_IPV6)
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

dnl borrowed from gnu findutils
dnl and relicensed as LGPL by the FSF

AC_ARG_WITH(prefetch,
  [  --with-prefetch=[yes/no]  Install GDM library prefetch utility [default=no]],,
  with_prefetch=no)

if test x$with_prefetch != xno ; then
	GDMPREFETCH="gdmprefetch"
        GDMPREFETCHLIST="gdmprefetchlist"
        GDMPREFETCHCMD='${libdir}/gdmprefetch \@${gdmconfdir}/gdmprefetchlist'
fi

AC_ARG_WITH(afs,
[  --with-afs              support -fstype afs],
[  AC_DEFINE(AFS, [], [Define if you have the Andrew File System])
  CPPFLAGS="$CPPFLAGS -I/usr/afsws/include"
  LIBS="$LIBS -L/usr/afsws/lib -L/usr/afsws/lib/afs -lsys -lrx -llwp"])

AC_MSG_CHECKING(how to get filesystem type)
fstype=no
# The order of these tests is important.
AC_TRY_CPP([#include <sys/statvfs.h>
#include <sys/fstyp.h>], AC_DEFINE(FSTYPE_STATVFS, [],
							[Define to use SVR4 statvfs to get filesystem type]) fstype=SVR4)
if test $fstype = no; then
AC_TRY_CPP([#include <sys/statfs.h>
#include <sys/fstyp.h>], AC_DEFINE(FSTYPE_USG_STATFS, [],
							[Define to use SVR3.2 statfs to get filesystem type]) fstype=SVR3)
fi
if test $fstype = no; then
AC_TRY_CPP([#include <sys/statfs.h>
#include <sys/vmount.h>], AC_DEFINE(FSTYPE_AIX_STATFS, [],
							 [Define to use AIX3 statfs to get filesystem type]) fstype=AIX)
fi
if test $fstype = no; then  
AC_TRY_CPP([#include <mntent.h>], AC_DEFINE(FSTYPE_MNTENT, [],
						    [Define to use 4.3BSD getmntent to get filesystem typ]) fstype=4.3BSD)
fi
if test $fstype = no; then  
AC_EGREP_HEADER(f_type;, sys/mount.h, AC_DEFINE(FSTYPE_STATFS, [],
							   [Define to use 4.4BSD and OSF1 statfs to get filesystem typ]) fstype=4.4BSD/OSF1)
fi
if test $fstype = no; then  
AC_TRY_CPP([#include <sys/mount.h>
#include <sys/fs_types.h>], AC_DEFINE(FSTYPE_GETMNT, [],
							   [Define to use Ultrix getmnt to get filesystem typ]) fstype=Ultrix)
fi
AC_MSG_RESULT($fstype)

dnl ****************************
dnl    IPv6 checks end here
dnl ****************************

dnl ## gdmopen building
dnl There are perhaps others where this works
GDMOPEN=
AC_CHECK_HEADERS(sys/vt.h, [
		 AC_DEFINE(HAVE_SYS_VT_H)])
AC_CHECK_HEADERS(sys/vt.h, [
		 GDMOPEN=gdmopen])
AC_SUBST(GDMOPEN)

dnl ## Authentication scheme
have_pam=no
VRFY="verify-crypt"
if test x$enable_authentication_scheme != xcrypt -a \
	x$enable_authentication_scheme != xshadow ; then
	AC_CHECK_HEADERS(security/pam_appl.h, [
		have_pam=yes
        	EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lpam"
        	VRFY="verify-pam"
        	AC_DEFINE(HAVE_PAM)])
fi

GDMASKPASS=
EXTRA_GDMASKPASS_LIBS=
if test x$have_pam = xyes ; then
	AC_CHECK_HEADERS(security/pam_misc.h, [
			 GDMASKPASS=gdmaskpass])
	if test x$GDMASKPASS != x ; then
		AC_CHECK_LIB(pam,misc_conv,,[AC_CHECK_LIB(pam_misc,misc_conv, [
		     EXTRA_GDMASKPASS_LIBS="$EXTRA_GDMASKPASS_LIBS -lpam_misc"], [
		     GDMASKPASS=], [
		     -lpam])])
	fi
fi
AC_SUBST(GDMASKPASS)
AC_SUBST(EXTRA_GDMASKPASS_LIBS)

if test x$enable_authentication_scheme = xpam -a x$have_pam = xno ; then
	AC_MSG_ERROR(PAM support requested but not available)
fi

if test x$have_pam = xno; then

  # Check if -lcrypt is necessary, and if so
  # add it to the front of the link chain
  AC_CHECK_LIB(crypt, crypt, [
  	      EXTRA_DAEMON_LIBS="-lcrypt $EXTRA_DAEMON_LIBS"])

  # Check if crypt lives in a separate header file
  AC_CHECK_HEADERS(crypt.h, [
		AC_DEFINE(HAVE_CRYPT)])

  if test x$enable_authentication_scheme = xshadow ; then
  	VRFY="verify-shadow"
	AC_DEFINE(HAVE_SHADOW)
  elif test x$enable_authentication_scheme != xcrypt ; then
  	# Check for shadow passwords (hack)
  	AC_MSG_CHECKING(for /etc/shadow)

  	if test -f /etc/shadow; then
		VRFY="verify-shadow"
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SHADOW)
  	else	
		AC_MSG_RESULT(no)
  	fi
  fi

  # Check How to handle authentication with the functions 'loginrestrictions',
  # 'passwdexpired', 'chpass', 'setpwdb', 'getuserpw', 'putuserpw' and 'endpwdb'
  AC_CHECK_FUNCS(loginrestrictions)
  can_clear_admchg_flag=yes
  AC_CHECK_FUNCS([passwdexpired chpass setpwdb getuserpw putuserpw endpwdb],
  		  ,can_clear_admchg_flag=no)
  if test x$can_clear_admchg_flag = xyes ; then
    AC_COMPILE_IFELSE([
      #if !defined(S_READ) || !defined(S_WRITE) || !defined(PW_ADMCHG)
      choke me
      #endif
      ],
      ,[AC_CHECK_HEADERS(
	usersec.h
	,[AC_COMPILE_IFELSE([
	  #include <usersec.h>
	  #if !defined(S_READ) || !defined(S_WRITE) || !defined(PW_ADMCHG)
	  choke me
	  #endif
	  ],
	  ,
	  can_clear_admchg_flag=no
	)]
	,can_clear_admchg_flag=no
      )]
    )
  fi
  if test x$can_clear_admchg_flag = xyes ; then
    AC_DEFINE(CAN_CLEAR_ADMCHG,,[Define this variable if the code to clear
    	      the ADMCHG flag can be compiled])
  fi
fi

AC_SUBST(VRFY)

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
		AC_DEFINE(HAVE_LOGINCAP)])

#
# Check for sys/sockio.h
#
AC_CHECK_HEADERS(sys/sockio.h, [
		 AC_DEFINE(HAVE_SYS_SOCKIO_H)])

#
# Check for libgen.h
#
AC_CHECK_HEADERS(libgen.h, [
		 AC_DEFINE(HAVE_LIBGEN_H)])


#
# Xdmcp checking
#
XDMCP_LIBS=""
if test x$with_xdmcp != xno ; then
	xdmcp_save_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $X_CFLAGS"
	AC_CHECK_HEADER(X11/Xdmcp.h, [
		AC_CHECK_LIB(Xdmcp, XdmcpFlush, [
		     	AC_DEFINE(HAVE_LIBXDMCP)
		     	XDMCP_LIBS="-lXdmcp"
		     	XDMCP_SUPPORT=yes],,[$X_LIBS -lX11 $X_EXTRA_LIBS])
	],,[#include <X11/Xproto.h>])

	if test x$with_xdmcp = xyes -a x$XDMCP_SUPPORT = x ; then
		AC_MSG_ERROR(XDMCP support requested but XDMCP libraries not found)
	fi
	CPPFLAGS="$xdmcp_save_CPPFLAGS"
fi
AC_SUBST(XDMCP_LIBS)

if test x$XDMCP_SUPPORT = xyes ; then
	AM_CONDITIONAL(XDMCP_SUPPORT, true)
	GDMCHOOSER=gdmchooser
	GDMXNESTCHOOSER=gdmXnestchooser
else
        # No need for TCP Wrappers in case XDMCP is disabled
        if test x$with_tcp_wrappers != xno ; then
	        echo "TCP wrappers not needed if XDMCP is not enabled"
        fi
	with_tcp_wrappers=no
	AM_CONDITIONAL(XDMCP_SUPPORT, false)
	GDMCHOOSER=
	GDMXNESTCHOOSER=
fi
AC_SUBST(GDMCHOOSER)
AC_SUBST(GDMXNESTCHOOSER)

#
# TCP Wrappers for XDMCP access control
#
if test x$with_tcp_wrappers = xno ; then
	echo "TCP wrappers disabled"
else
  AC_MSG_CHECKING([whether to use TCP wrappers])
  LIBWRAP_PATH=""
  case "$host" in
    *-*-solaris*)
	if test -f /usr/sfw/lib/libwrap.so; then
		LIBWRAP_PATH=/usr/sfw/lib/libwrap.so
		echo "Found $LIBWRAP_PATH" 1>&5
	fi
	EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -L/usr/sfw/lib -R/usr/sfw/lib -lwrap"
	;;
    *)
        for I in $LDFLAGS $LIBS -L/usr/lib; do
		case "$I" in
		-L*)
			THEFILE="`echo $I | sed -e 's,^-L,,'`"
			echo "From $I, checking in dir $THEFILE for libwrap.a" 1>&5
			if test -f $THEFILE/libwrap.a; then
				LIBWRAP_PATH=$THEFILE/libwrap.a
				echo "Found $LIBWRAP_PATH" 1>&5
				break
			fi
			;;
		esac
	done
	;;
  esac

  if test -n "$LIBWRAP_PATH"; then
        AC_MSG_RESULT(yes)
  else
        AC_MSG_RESULT(no)
  fi

  if test -n "$LIBWRAP_PATH"; then
	nm $LIBWRAP_PATH | grep 'T setenv' && LIBWRAP_PATH=""
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
                AC_DEFINE(HAVE_TCPWRAPPERS)
	fi
  fi

  case "$host" in
    *-*-solaris*)
	# No need for extra libwrap checking.
	;;
    *)
	if test -n "$LIBWRAP_PATH"; then
	AC_CHECK_LIB(wrap, hosts_ctl, [
		EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lwrap"], [
		EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS $LIBWRAP_PATH"])
	fi

	dnl find out if we need -lnsl or whatever
	LIB_NSL=
	if test -n "$LIBWRAP_PATH"; then
		AC_MSG_CHECKING(whether -lwrap also requires -lnsl)
		ORIG_LIBS="$LIBS"
		LIBS="$EXTRA_DAEMON_LIBS $LIBS"
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
	;;
   esac
fi

AC_MSG_CHECKING([for corrupt government])
AC_MSG_RESULT(yes)

have_xinput=
AC_CHECK_LIB(Xi, XOpenDevice, XINPUT_LIBS=-lXi)
if test "x$XINPUT_LIBS" = x; then
  save_LIBS="$LIBS"
  for xinputpath in $x_libraries /usr/X11R6/lib /usr/openwin/lib; do
    LIBS="-L$xinputpath -lXi"
    case "$host" in
       *solaris*) XINPUT_RPATH_FLAGS="-R$xinputpath" 
       ;;
    esac
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
   AC_DEFINE(HAVE_XINPUT)
   X_EXTRA_LIBS="$X_EXTRA_LIBS $XINPUT_LIBS"
fi

#
# X11 Xinerama extension
#
# Check for Xinerama extension (Solaris impl or Xfree impl)
xinerama_save_cppflags="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $X_CFLAGS"

# Check for defopen
#
if test -f /etc/default/login; then
  AC_CHECK_LIB(cmd, defopen, use_defopen=yes, use_defopen=no)
  if test "x$use_defopen" = "xyes"; then
    EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lcmd"
    AC_DEFINE(HAVE_DEFOPEN)
  fi
fi

ALL_X_LIBS="$X_LIBS $X_PRE_LIBS -lX11 $X_EXTRA_LIBS"

if test ! x$with_xinerama = xno ; then
  case "$host" in
    *-*-solaris*)
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
                            AC_DEFINE(HAVE_SOLARIS_XINERAMA)
                            AC_DEFINE(HAVE_XINERAMA)
		     	    XINERAMA_LIBS=""
		     	    XINERAMA_SUPPORT=yes,
                            use_solaris_xinerama=no,
                            [#include <X11/Xlib.h>])
	fi
        AC_MSG_CHECKING(for Xinerama support on Solaris)
	AC_MSG_RESULT($use_solaris_xinerama);
	;;
    *)
      	# Check for XFree
	use_xfree_xinerama=yes
	AC_CHECK_LIB(Xinerama, XineramaQueryExtension,
            [AC_CHECK_HEADER(X11/extensions/Xinerama.h,
                             if test -z "`echo $ALL_X_LIBS | grep "\-lXext" 2> /dev/null`"; then
  				 X_EXTRA_LIBS="-lXext $X_EXTRA_LIBS"
                             fi
                             AC_DEFINE(HAVE_XFREE_XINERAMA)
                             AC_DEFINE(HAVE_XINERAMA)
		     	     XINERAMA_LIBS="-lXinerama"
		     	     XINERAMA_SUPPORT=yes,
                             use_xfree_xinerama=no,
                             [#include <X11/Xlib.h>])],
            use_xfree_xinerama=no, -lXext $ALL_X_LIBS)
	AC_MSG_CHECKING(for Xinerama support on XFree86)
	AC_MSG_RESULT($use_xfree_xinerama);
	;;
  esac
fi

AC_SUBST(XINERAMA_LIBS)
CPPFLAGS="$xinerama_save_cppflags"

#
# Distributed Multihead X extension (DMX)
#
DMX_SUPPORT=""
DMX_LIBS=""
if test x$with_dmx != xno ; then
	dmx_save_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $X_CFLAGS"
	AC_CHECK_HEADER(X11/extensions/dmxext.h, [
		AC_CHECK_LIB(dmx, DMXQueryExtension, [
			DMX_LIBS="-ldmx"
			DMX_SUPPORT=yes],,[$X_LIBS -lX11 $X_EXTRA_LIBS])
	],,[#include <X11/Xlib.h>])

	if test x$with_dmx = xyes -a x$DMX_SUPPORT = x ; then
		AC_MSG_ERROR(DMX support requested but DMX librariy not found)
	fi
	CPPFLAGS="$dmx_save_CPPFLAGS"
fi
AC_SUBST(DMX_LIBS)
AM_CONDITIONAL(DMX_SUPPORT, test x$DMX_SUPPORT = xyes)

#
# Solaris Trusted Extensions stuff
#
case "$host" in
*solaris*)
        AC_CHECK_HEADERS(sys/tsol/label_macro.h,
                AC_DEFINE(HAVE_TSOL, ,[Building with TSOL support]) found_tsol=yes,)
        ;;
*)
        ;;
esac

AM_CONDITIONAL(TSOL_DEFINED, test x$found_tsol = xyes)
if test "x$found_tsol" = "xyes" ; then
	EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lsecdb"
fi


#
# SELinux stuff
#
if test "x$with_selinux" = "xyes" ; then
	echo "SELinux support requested (--with-selinux was given), checking prerequisites"
	if test -d /usr/local/selinux/include ; then
		echo "Found /usr/local/selinux/include, will look there for SELinux stuff as well"
		CFLAGS="$CFLAGS -I/usr/local/selinux/include"
		LDFLAGS="$LDFLAGS -L/usr/local/selinux/lib"
	fi
	AC_CHECK_HEADER(selinux/selinux.h)
	AC_CHECK_HEADER(selinux/get_context_list.h)
	AC_CHECK_LIB(attr,attr_get,/bin/true)
	# I'm anal and I don't know which versions have which symbols,
	# so I check for all the ones we use
	AC_CHECK_LIB(selinux,get_ordered_context_list,/bin/true)
	AC_CHECK_LIB(selinux,freecon,/bin/true)
	AC_CHECK_LIB(selinux,freeconary,/bin/true)
	AC_CHECK_LIB(selinux,setexeccon,/bin/true)
	AC_CHECK_LIB(selinux,is_selinux_enabled,/bin/true)
	AC_DEFINE(HAVE_SELINUX)
	EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lselinux -lattr"
fi

#
# ConsoleKit support
#
use_console_kit=no
if test "x$with_console_kit" != "xno" ; then
	use_console_kit=yes
	PKG_CHECK_MODULES(DBUS, dbus-glib-1 >= $DBUS_REQUIRED)
	AC_DEFINE(WITH_CONSOLE_KIT, 1, [Define to enable ConsoleKit support])
fi
AM_CONDITIONAL(WITH_CONSOLE_KIT, test x$use_console_kit = xyes)
AC_SUBST(WITH_CONSOLE_KIT)
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

#
# Define some variables to represent the directories we use.
#
AC_SUBST(authdir, ${localstatedir}/gdm)
AC_SUBST(gdmlocaledir, ${gdmconfdir})
AC_SUBST(pixmapdir, ${datadir}/pixmaps)

AC_ARG_WITH(log-dir,    [  --with-log-dir=<file>     log dir])

if ! test -z "$with_log_dir"; then
   GDM_LOG_DIR=$with_log_dir
else
   GDM_LOG_DIR=/var/log/gdm
fi
AC_SUBST(logdir, $GDM_LOG_DIR)

AC_ARG_WITH(libaudit,
  [  --with-libaudit=[auto/yes/no]  Add Linux audit support [default=auto]],,
  with_libaudit=auto)

withval=""
AC_ARG_WITH(at-bindir,
[  --with-at-bindir=<PATH>   PATH to Accessible Technology programs [default=BINDIR]],)

if test x$withval != x; then
	AT_BINDIR="$withval"
else
	AT_BINDIR='${bindir}'
fi

AC_SUBST(AT_BINDIR)

withval=""
AC_ARG_WITH(defaults_conf,
[  --with-defaults-conf=<FILENAME>   FILENAME to give to defaults file [default=DATADIR/gdm/defaults.conf]],)

if test x$withval != x; then
	GDM_DEFAULTS_CONF="$withval"
else
	GDM_DEFAULTS_CONF='${datadir}/gdm/defaults.conf'
fi

AC_SUBST(GDM_DEFAULTS_CONF)

withval=""
AC_ARG_WITH(custom_conf,
[  --with-custom-conf=<FILENAME>   FILENAME to give to custom configuration file [default=GDMCONFDIR/custom.conf]],)

if test x$withval != x; then
	GDM_CUSTOM_CONF="$withval"
else
	GDM_CUSTOM_CONF='${gdmconfdir}/custom.conf'
fi

AC_SUBST(GDM_CUSTOM_CONF)
AC_SUBST(GDM_OLD_CONF, '${gdmconfdir}/gdm.conf')

AC_ARG_WITH(prefetch,
  [  --with-prefetch=[yes/no]  Install GDM library prefetch utility [default=no]],,
  with_prefetch=no)

if test x$with_prefetch != xno ; then
	GDMPREFETCH="gdmprefetch"
        GDMPREFETCHLIST="gdmprefetchlist"
        GDMPREFETCHCMD='${libdir}/gdmprefetch \@${gdmconfdir}/gdmprefetchlist'
fi
AC_SUBST(GDMPREFETCH)
AC_SUBST(GDMPREFETCHLIST)
AC_SUBST(GDMPREFETCHCMD)

AC_ARG_ENABLE(secureremote,
  [  --enable-secureremote=[yes/no]  Enable to offer a secure X connection through ssh  [default=no]],,
  enable_secureremote=no)

AC_PATH_PROG(ZENITY,zenity,no)
if test "x$ZENITY" = "xno" ; then
  if test "x$enable_secureremote" = "xyes" ; then
    AC_MSG_ERROR(Secure remote connection requested but zenity binary not found)
  fi
  # if it was no, nothing happens, if it was auto, well then we're out of luck
  enable_console_helper=no
else
  if test ! "x$enable_secureremote" = "xno" ; then
    enable_secureremote=yes
  fi
fi

if test ${exec_prefix} != "NONE"; then
   real_gdm_prefix=$exec_prefix
elif test ${prefix} != "NONE"; then
   real_gdm_prefix=$prefix
else
   real_gdm_prefix=$ac_default_prefix
fi

if test ${libexecdir} = "\${exec_prefix}/libexec"; then
   ssh_libexec="$real_gdm_prefix/libexec"
else
   ssh_libexec="${libexecdir}"
fi

if test x$enable_secureremote != xno ; then
       SSHDESKTOP="ssh.desktop"
        GDMSSHSESSION="gdm-ssh-session"
        GDMSSHSESSIONCMD="${ssh_libexec}/gdm-ssh-session"
fi
AC_SUBST(SSHDESKTOP)
AC_SUBST(GDMSSHSESSION)
AC_SUBST(GDMSSHSESSIONCMD)

AC_ARG_WITH(xevie,
  [  --with-xevie=[yes/no]  Add XEvIE Xserver extension support [default=no]],,
  with_xevie=no)

if test x$with_xevie != xno ; then
	XEVIE_OPTION="+extension XEVIE"
else
	XEVIE_OPTION=""
fi

AC_SUBST(XEVIE_OPTION)
AC_DEFINE_UNQUOTED(XEVIE_OPTION,"$XEVIE_OPTION")

AC_MSG_CHECKING(for Solaris fbconsole)
if test -x /usr/openwin/bin/fbconsole; then
   AC_DEFINE(HAVE_FBCONSOLE)
   AC_MSG_RESULT(yes)
else
   AC_MSG_RESULT(no)
fi

# Check for Linux auditing API
#
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
    EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -laudit"
    AC_DEFINE(HAVE_LIBAUDIT,1,[linux audit support])
fi

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
   AC_DEFINE(HAVE_ADT)
   EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lbsm"
   AC_MSG_RESULT(yes)
else
   AC_MSG_RESULT(no)
fi

# Check for Solaris logindevperm support
#
AC_MSG_CHECKING(for Solaris di_devperm_login)
AC_CHECK_LIB(devinfo, di_devperm_login, [
		     	AC_DEFINE(HAVE_LOGINDEVPERM)
			    EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -ldevinfo" ])

# Check for RBAC support (chkauthattr
#
AC_MSG_CHECKING(for RBAC support - chkauthattr)
AC_CHECK_LIB(secdb, chkauthattr, [
		     	AC_DEFINE(HAVE_CHKAUTHATTR)
			    EXTRA_DAEMON_LIBS="$EXTRA_DAEMON_LIBS -lsecdb"
			    EXTRA_GREETER_LIBS="$EXTRA_GREETER_LIBS -lsecdb"])

# ctrun support for Solaris
#
AC_ARG_WITH(ctrun,
  [  --with-ctrun=[yes/no]  Define to yes if GDM is started as a svcadm(1M) service,  [default=no]],,
  with_ctrun=no)

if test x$with_ctrun != xno ; then
   AC_PATH_PROG(HAVE_CTRUN,ctrun,no,/usr/bin)
   if test "x$HAVE_CTRUN" != "xno" ; then
      AC_DEFINE(HAVE_CTRUN)
   fi
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
AC_SUBST(EXTRA_GREETER_LIBS)

# Define different Halt, and Reboot commands differently for Solaris.
#
# On Solaris/bin is a symlink to /usr/bin, so don't include it in
# GDM_USER_PATH.
#
GDM_USER_PATH=""
GDM_RBAC_SYSCMD_KEYS=""

case "$host" in
*solaris*)
        GDM_USER_PATH="/usr/bin"
        HALT_COMMAND="/sbin/init 5"
        REBOOT_COMMAND="/sbin/init 6"
        SUSPEND_COMMAND="/usr/openwin/bin/sys-suspend -n"
        XSESSION_SHELL=/bin/ksh
        SOUND_PROGRAM=/usr/bin/audioplay
        GDMGNOMESESSIONCMD=/usr/dt/config/Xsession.jds
        GDM_RBAC_SYSCMD_KEYS="HALT:solaris.system.shutdown;REBOOT:solaris.system.shutdown"
        ;;
*freebsd*)
	GDM_USER_PATH="/bin:/usr/bin"
	HALT_COMMAND="/sbin/shutdown -p now;/usr/sbin/shutdown -p now"
	REBOOT_COMMAND="/sbin/shutdown -r now;/usr/sbin/shutdown -r now"
	SUSPEND_COMMAND="/usr/sbin/zzz"
	XSESSION_SHELL=/bin/sh
	SOUND_PROGRAM=
        GDMGNOMESESSIONCMD=gnome-session
	;;
*)
        GDM_USER_PATH="/bin:/usr/bin"
        HALT_COMMAND="/usr/bin/poweroff;/sbin/poweroff;/sbin/shutdown -h now;/usr/sbin/shutdown -h now"
        REBOOT_COMMAND="/usr/bin/reboot;/sbin/reboot;/sbin/shutdown -r now;/usr/sbin/shutdown -r now"
        SUSPEND_COMMAND=""
        XSESSION_SHELL=/bin/sh
        SOUND_PROGRAM=/usr/bin/play
        GDMGNOMESESSIONCMD=gnome-session
        ;;
esac

# On Solaris, the /usr/bin/Xserver script is used to decide whether to
# use Xsun or Xorg, so this is used on Solaris.
#
# When testing for /usr/X11R6, first check with "! -h" for /usr/X11R6
# and /usr/X11 since they often symlink to each other, and configure
# should use the more stable location (the real directory) if possible.
#
if test -x /usr/X11/bin/Xserver; then
   X_PATH="/usr/X11/bin"
   X_SERVER_PATH="/usr/X11/bin"
   X_SERVER="/usr/X11/bin/Xserver"
   GDM_USER_PATH="$GDM_USER_PATH:/usr/X11/bin"
   X_CONFIG_OPTIONS="-audit 0"
elif test ! -h /usr/X11R6 -a ! -h /usr/X11R6/bin -a -x /usr/X11R6/bin/X; then
   X_PATH="/usr/X11R6/bin"
   X_SERVER_PATH="/usr/X11R6/bin"
   X_SERVER="/usr/X11R6/bin/X"
   GDM_USER_PATH="$GDM_USER_PATH:/usr/X11R6/bin"
   X_CONFIG_OPTIONS="-audit 0"
elif test ! -h /usr/X11 -a ! -h /usr/X11/bin -a -x /usr/X11/bin/X; then
   X_PATH="/usr/X11/bin"
   X_SERVER_PATH="/usr/X11/bin"
   X_SERVER="/usr/X11/bin/X"
   GDM_USER_PATH="$GDM_USER_PATH:/usr/X11/bin"
   X_CONFIG_OPTIONS="-audit 0"
elif test -x /usr/bin/X; then
   X_PATH="/usr/bin"
   X_SERVER_PATH="/usr/bin"
   X_SERVER="/usr/bin/X"
   X_CONFIG_OPTIONS="-audit 0"
elif test -x /usr/bin/Xorg; then
   X_PATH="/usr/bin"
   X_SERVER_PATH="/usr/bin"
   X_SERVER="/usr/bin/Xorg"
   X_CONFIG_OPTIONS="-audit 0"
elif test -x /usr/X11R6/bin/X; then
   X_PATH="/usr/X11R6/bin"
   X_SERVER_PATH="/usr/X11R6/bin"
   X_SERVER="/usr/X11R6/bin/X"
   GDM_USER_PATH="$GDM_USER_PATH:/usr/X11R6/bin"
   X_CONFIG_OPTIONS="-audit 0"
elif test -x /usr/X11/bin/X; then
   X_PATH="/usr/X11/bin"
   X_SERVER_PATH="/usr/X11/bin"
   X_SERVER="/usr/X11/bin/X"
   GDM_USER_PATH="$GDM_USER_PATH:/usr/X11/bin"
   X_CONFIG_OPTIONS="-audit 0"
elif test -x /usr/openwin/bin/Xsun; then
   # Do not add /usr/openwin/bin here because on Solaris you need 
   # /usr/openwin/bin in your PATH even if you are using the Xorg
   # Xserver.  We add this to the path below.
   X_PATH="/usr/openwin/bin"
   X_SERVER_PATH="/usr/openwin/bin"
   X_SERVER="/usr/openwin/bin/Xsun"
   X_CONFIG_OPTIONS="-audit 0 -nobanner"
elif test -x /opt/X11R6/bin/X; then
   X_PATH="/opt/X11R6/bin"
   X_SERVER_PATH="/opt/X11R6/bin"
   X_SERVER="/opt/X11R6/bin/X"
   GDM_USER_PATH="$GDM_USER_PATH:/opt/X11R6/bin"
   X_CONFIG_OPTIONS="-audit 0"   
elif test -x /usr/bin/X; then
   X_PATH="/usr/bin"
   X_SERVER_PATH="/usr/bin"
   X_SERVER="/usr/bin/X"
   X_CONFIG_OPTIONS="-audit 0"
else
   # what to do, what to do, this is wrong, but this just sets the
   # defaults, perhaps this user is cross compiling or some such
   X_PATH="/usr/bin/X11:/usr/X11R6/bin:/opt/X11R6/bin"
   X_SERVER_PATH="/usr/X11R6/bin"
   X_SERVER="/usr/X11R6/bin/X"
   GDM_USER_PATH="$GDM_USER_PATH:/usr/bin/X11:/usr/X11R6/bin:/usr/local/bin:/opt/X11R6/bin"
   X_CONFIG_OPTIONS="-audit 0"
fi

# Don't add bindir to GDM_USER_PATH if it is already in the PATH.
# If it is /bin or /usr/bin, for example.
if test "x$real_gdm_prefix" != "x/" -a "x$real_gdm_prefix" != "x/usr"; then
    GDM_USER_PATH="$GDM_USER_PATH:${bindir}"
fi


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

# Set POST_PATH after doing system specific case above just in case
# any platforms want to update the PATH for a specific OS or distro.
# None currently, but might be useful.
#
if test "x$USER_POST_PATH" != "x"; then
   GDM_USER_PATH="$GDM_USER_PATH:$USER_POST_PATH"
fi

dnl ---------------------------------------------------------------------------
dnl Expand vars
dnl ---------------------------------------------------------------------------

AS_AC_EXPAND(LOCALSTATEDIR, $localstatedir)
AS_AC_EXPAND(SYSCONFDIR, $sysconfdir)
AS_AC_EXPAND(DATADIR, $datadir)
AS_AC_EXPAND(BINDIR, $bindir)
AS_AC_EXPAND(SBINDIR, $sbindir)
AS_AC_EXPAND(LIBDIR, $libdir)
AS_AC_EXPAND(LIBEXECDIR, $libexecdir)

dnl ---------------------------------------------------------------------------
dnl - PID file
dnl ---------------------------------------------------------------------------

AC_ARG_WITH(pid-file,    [  --with-pid-file=<file>     pid file])

if ! test -z "$with_pid_file"; then
   GDM_PID_FILE=$with_pid_file
else
   GDM_PID_FILE=/var/run/gdm.pid
fi

AC_SUBST(GDM_PID_FILE)
AC_DEFINE_UNQUOTED(GDM_PID_FILE, "$GDM_PID_FILE", [pid file])

dnl ---------------------------------------------------------------------------
dnl - Additional warnings
dnl ---------------------------------------------------------------------------

AC_ARG_ENABLE(more-warnings,
  AC_HELP_STRING([--enable-more-warnings], [Maximum compiler warnings]),
set_more_warnings="$enableval",[
if test -d $srcdir/.svn; then
        set_more_warnings=yes
else
        set_more_warnings=no
fi
])
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

# Allow configure to specify RBAC keys.
#
AC_ARG_WITH(rbac-system-command-keys,    [  --with-rbac-system-command-keys=<keys>     RBAC system command keys])

if ! test -z "$with_system_command_keys"; then
   GDM_RBAC_SYSCMD_KEYS=$with_system_command_keys
fi

# Set configuration choices.
#
AC_SUBST(HALT_COMMAND)
AC_DEFINE_UNQUOTED(HALT_COMMAND,"$HALT_COMMAND")
AC_SUBST(REBOOT_COMMAND)
AC_DEFINE_UNQUOTED(REBOOT_COMMAND,"$REBOOT_COMMAND")
AC_SUBST(SUSPEND_COMMAND)
AC_DEFINE_UNQUOTED(SUSPEND_COMMAND,"$SUSPEND_COMMAND")
AC_SUBST(XSESSION_SHELL)
AC_DEFINE_UNQUOTED(XSESSION_SHELL,"$XSESSION_SHELL")
AC_SUBST(GDM_USER_PATH)
AC_DEFINE_UNQUOTED(GDM_USER_PATH,"$GDM_USER_PATH")
AC_SUBST(SOUND_PROGRAM)
AC_DEFINE_UNQUOTED(SOUND_PROGRAM,"$SOUND_PROGRAM")
AC_SUBST(GDMGNOMESESSIONCMD)
AC_SUBST(GDM_RBAC_SYSCMD_KEYS)
AC_DEFINE_UNQUOTED(GDM_RBAC_SYSCMD_KEYS,"$GDM_RBAC_SYSCMD_KEYS")

AC_SUBST(X_PATH)
AC_SUBST(X_SERVER)
AC_SUBST(X_SERVER_PATH)
AC_SUBST(X_CONFIG_OPTIONS)
AC_DEFINE_UNQUOTED(X_SERVER,"$X_SERVER")
AC_DEFINE_UNQUOTED(X_SERVER_PATH,"$X_SERVER_PATH")
AC_DEFINE_UNQUOTED(X_CONFIG_OPTIONS,"$X_CONFIG_OPTIONS", Options used when launching xserver)

AC_SUBST(X_XNEST_CMD)
AC_SUBST(X_XNEST_CONFIG_OPTIONS)
AC_SUBST(X_XNEST_UNSCALED_FONTPATH)
AC_DEFINE_UNQUOTED(X_XNEST_CMD,"$X_XNEST_CMD")
AC_DEFINE_UNQUOTED(X_XNEST_CONFIG_OPTIONS,"$X_XNEST_CONFIG_OPTIONS")
AC_DEFINE_UNQUOTED(X_XNEST_UNSCALED_FONTPATH,"$X_XNEST_UNSCALED_FONTPATH")

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

AC_OUTPUT([
Makefile
daemon/Makefile
gui/Makefile
gui/gdmflexiserver-xnest.desktop.in
gui/faces/Makefile
gui/greeter/Makefile
gui/greeter/themes/Makefile
gui/greeter/themes/circles/Makefile
gui/greeter/themes/happygnome/Makefile
gui/greeter/themes/happygnome-list/Makefile
gui/modules/Makefile
utils/Makefile
pixmaps/Makefile
pixmaps/16x16/Makefile
pixmaps/32x32/Makefile
pixmaps/48x48/Makefile
config/Makefile
po/Makefile.in
common/Makefile
docs/Makefile
config/Init
config/PreSession
config/PostSession
config/gdmprefetchlist
config/gnome.desktop.in
config/ssh.desktop.in
])

dnl add back when help comes back
dnl docs/C/Makefile
dnl omf-install/Makefile

dnl <= Configuration summary =>

echo ""
echo "GDM2 configuration summary:"
echo "==========================="
echo ""

dnl <= CFLAGS and LDFLAGS =>
echo "CFLAGS : $CFLAGS"
echo "LDFLAGS : $LDFLAGS"
echo ""

dnl <= Prefixes =>
echo "prefix : $prefix"
echo "sysconf : $sysconfdir"
echo "sysconfsubdir : $sysconfsubdir"
echo "gdmconf dir : $gdmconfdir"
echo "dmconf dir : $dmconfdir"
echo "bin dir : $bindir"
echo "sbin dir : $sbindir"
echo "PAM prefix : $PAM_PREFIX"
echo "data dir : $datadir"
echo "X server : $X_SERVER"
echo ""

dnl <= TCP Wrappers support? =>
if test x"$LIBWRAP_PATH" = x ; then
	echo "TCP Wrappers support                  : NO"
else
	echo "TCP Wrappers support                  : YES"
fi

dnl <= XINERAMA =>
if test x"$XINERAMA_SUPPORT" = xyes ; then
	echo "Xinerama support                      : YES"
else
	echo "Xinerama support                      : NO"
fi

dnl <= XDMCP =>
if test x"$XDMCP_SUPPORT" = xyes ; then
	echo "XDMCP (remote login) support          : YES"
else
	echo "XDMCP (remote login) support          : NO"
fi

dnl <= Secure remote connection =>
if test x"$enable_secureremote" = xyes ; then
	echo "Secure remote connection              : YES"
else
	echo "Secure remote connection              : NO"
fi


dnl <= DMX =>
if test x"$DMX_SUPPORT" = xyes ; then
	echo "DMX (Distributed Multihead X) support : YES"
else
	echo "DMX (Distributed Multihead X) support : NO"
fi

dnl <= Console Helper =>
if test "x$enable_console_helper" = "xyes"; then
	echo "Console helper                        : YES"
else
	echo "Console helper                        : NO"
fi

dnl <= SELinux support =>
if test "x$with_selinux" = "xyes" ; then
	echo "SELinux support                       : YES"
else
	echo "SELinux support                       : NO"
fi

dnl <= Solaris Trusted Extensions support =>
if test "x$found_tsol" = "xyes" ; then
	echo "Solaris Trusted Extensions support    : YES"
else
	echo "Solaris Trusted Extensions support    : NO"
fi

dnl <= ConsoleKit support =>
if test "x$use_console_kit" = "xyes" ; then
	echo "ConsoleKit support                    : YES"
else
	echo "ConsoleKit support                    : NO"
fi

dnl <= Authentication scheme =>
echo "Authentication scheme                 : $VRFY"

dnl <= Utils built =>
echo "Extra utilities built                 : "`echo $GDMOPEN $GDMASKPASS $GDMPREFETCH $GDMSSHSESSION`

echo ""
dnl <= End of configuration summary =>

