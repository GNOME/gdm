#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="GDM"

(test -f $srcdir/configure.in \
  && test -d $srcdir/daemon \
  && test -f $srcdir/daemon/gdm.h) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level gdm directory"
    exit 1
}

. $srcdir/macros/autogen.sh
