#!/bin/sh
#
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
    echo "/usr/bin/X"
elif test -x /usr/X11/bin/Xserver; then
    echo "/usr/X11/bin/Xserver"
elif test ! -h /usr/X11R6 -a -x /usr/X11R6/bin/X; then
    echo "/usr/X11R6/bin/X"
elif test ! -h /usr/X11 -a -x /usr/X11/bin/X; then
    echo "/usr/X11/bin/X"
elif test -x /usr/X11R6/bin/X; then
    echo "/usr/X11R6/bin/X"
elif test -x /usr/bin/Xorg; then
    echo "/usr/bin/Xorg"
elif test -x /usr/X11/bin/X; then
    echo "/usr/X11/bin/X"
elif test -x /usr/openwin/bin/Xsun; then
    echo "/usr/openwin/bin/Xsun"
elif test -x /opt/X11R6/bin/X; then
    echo "/opt/X11R6/bin/X"
else
    echo ""
fi
