#!/bin/sh
# Simple script I use to rebuild gdm on my system.  Should work on redhat
# and redhat like systems
if [ x$UPDATE = xyes ] ; then
	if [ x$CLEAN = xyes ] ; then
		echo make distclean
		if ! make distclean ; then
			echo 
			echo '*********' make distclean failed '*********'
			echo 
			exit 1
		fi
		CLEAN=no
	fi
	echo cvs -z3 update -dP
	if ! cvs -z3 update -dP ; then
		echo 
		echo '*********' cvs update failed '*********'
		echo 
		exit 1
	fi
fi
	
echo ./autogen.sh --prefix=/usr --sysconfdir=/etc/X11 --localstatedir=/var --enable-console-helper --with-pam-prefix=/etc "$@"
if ! ./autogen.sh --prefix=/usr --sysconfdir=/etc/X11 --localstatedir=/var --enable-console-helper --with-pam-prefix=/etc "$@" ; then
	echo 
	echo '*********' autogen.sh failed '*********'
	echo 
	exit 1
fi

if [ x$CLEAN = xyes ] ; then
	echo make clean
	if ! make clean ; then
		echo 
		echo '*********' make clean failed '*********'
		echo 
		exit 1
	fi
fi
echo make
if ! make ; then
	echo 
	echo '*********' make failed '*********'
	echo 
	exit 1
fi

echo 
echo '******************************************************'
echo 'Build of gdm finished, now type "make install" as root'
echo '******************************************************'
echo 
