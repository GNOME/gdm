#!/bin/sh

xgettext --default-domain=gdm --directory=.. \
  --add-comments --keyword=_ --keyword=N_ \
  --files-from=./POTFILES.in \
&& test ! -f gdm.po \
   || ( rm -f ./gdm.pot \
    && mv gdm.po ./gdm.pot )
