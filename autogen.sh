#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="GDM"

if test ! -d $srcdir/vicious-extensions; then
	echo -n "**Error**: vicious-extensions not found, please do a clean "
	echo "checkout or do: cvs -z3 get vicious-extensions"
	exit 1
fi

if grep VICIOUS_CFLAGS $srcdir/vicious-extensions/Makefile.am > /dev/null; then 
	echo "**Error**: Use the gnome-2-6 branch of vicious-extensions "
	echo "for now, you have to do: cvs -z3 update -rgnome-2-6"
	echo "in the vicious-extensions subdirectory."
	echo "This will be fixed up after 2.6 is released ..."
	exit 1
fi

(test -f $srcdir/configure.in \
  && test -d $srcdir/daemon \
  && test -f $srcdir/daemon/gdm.h) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level gdm directory"
    exit 1
}

which gnome-autogen.sh || {
    echo "You need to install gnome-common from the GNOME CVS"
    exit 1
}
USE_GNOME2_MACROS=1 . gnome-autogen.sh
