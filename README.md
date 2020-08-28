GDM - GNOME Display Manager
===========================
http://wiki.gnome.org/Projects/GDM/

The GNOME Display Manager is a system service that is responsible for
providing graphical log-ins and managing local and remote displays.

## Building and installing
To build and install GDM from source, just execute the following commands:

```
$ meson _build
$ ninja -C _build
$ sudo ninja -C _build install
```

## Contributing
You can browse the code, issues and more at GDM's [GitLab repository].

If you find a bug in GDM, please file an issue on the [issue tracker]. Please
try to add reproducible steps and the relevant version of GDM.

If you want to contribute functionality or bug fixes, please open a Merge
Request (MR). For more info on how to do this, see GitLab's [help pages on
MR's]. Please also follow the GDM coding style, which is documented in
`HACKING`.

If GDM is not translated in your language or you believe that the
current translation has errors, you can join one of the various translation
teams in GNOME. Translators do not commit directly to Git, but are advised to
use our separate translation infrastructure instead. More info can be found at
the [translation project wiki page].

## Licensing
GDM is licensed under the GNU General Public License v2.0. For more info, see
the `COPYING` file.


[help pages on MR's]: https://docs.gitlab.com/ee/gitlab-basics/add-merge-request.html
[GitLab repository]: https://gitlab.gnome.org/GNOME/gdm
[issue tracker]: https://gitlab.gnome.org/GNOME/gdm/issues
[translation project wiki page]: https://wiki.gnome.org/TranslationProject/
