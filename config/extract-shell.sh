#!/bin/sh
grep gettext | sed 's/^[^"]*\("[^"]*"\).*$/const char *foo = N_(\1);/'
