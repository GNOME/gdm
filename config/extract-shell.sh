#!/bin/sh
echo "/* DON\'T CHANGE THIS BY HAND! CHANGE THE SCRIPT THIS IS GENERATED */"
echo "/* ALWAYS ADD A CHANGELOG OR I WILL PERSONALLY KICK YOUR ASS! */"
grep gettextfunc | fgrep -v 'gettextfunc ()' | sed 's/^[^"]*\("[^"]*"\).*$/const char *foo = N_(\1);/'
