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

(test -f $srcdir/configure.in \
  && test -d $srcdir/daemon \
  && test -f $srcdir/daemon/gdm.h) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level gdm directory"
    exit 1
}

. $srcdir/macros/autogen.sh
