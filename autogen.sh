#!/bin/sh
# Run this to generate all the initial makefiles, etc.

REQUIRED_AUTOMAKE_VERSION=1.5
USE_GNOME2_MACROS=1

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="GDM"

(test -f $srcdir/configure.ac \
  && test -d $srcdir/daemon \
  && test -f $srcdir/daemon/gdm.h) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level gdm directory"
    exit 1
}

which gnome-autogen.sh || {
    echo "You need to install gnome-common from the GNOME SVN"
    exit 1
}
#USE_GNOME2_MACROS=1 . gnome-autogen.sh
. gnome-autogen.sh
