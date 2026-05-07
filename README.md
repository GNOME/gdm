GDM - GNOME Display Manager
===========================
https://gitlab.gnome.org/GNOME/gdm

The GNOME Display Manager is a system service that is responsible for
providing graphical log-ins and managing local and remote displays.

## Building and installing
To build and install GDM from source, just execute the following commands:

```
$ meson _build
$ ninja -C _build
$ sudo ninja -C _build install
```

## Configuration

GDM is configured through two main mechanisms:

### Daemon configuration

The daemon is configured via `custom.conf`, an INI-style key file usually
located at `/etc/gdm/` or `/etc/gdm3/`.
Settings in `/run/gdm/custom.conf` take priority over the configuration in
`/etc/gdm/` or `/etc/gdm3/` and can be used for runtime overrides.

For a list of available settings and their descriptions, see
[`data/gdm.schemas`](data/gdm.schemas).

### Login screen settings

The login screen appearance and behavior is controlled by GSettings keys
under the `org.gnome.login-screen` schema. These are configured via dconf
by creating a key file in `/etc/dconf/db/gdm.d/` and running `dconf update`.

For more dconf details, see [dconf(1)](https://man.archlinux.org/man/dconf.1).

For a list of available settings and their descriptions, see
[`data/org.gnome.login-screen.gschema.xml`](data/org.gnome.login-screen.gschema.xml).

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
the [Translation Project Welcome page].

## Licensing
GDM is licensed under the GNU General Public License v2.0. For more info, see
the `COPYING` file.


[help pages on MR's]: https://docs.gitlab.com/ee/gitlab-basics/add-merge-request.html
[GitLab repository]: https://gitlab.gnome.org/GNOME/gdm
[issue tracker]: https://gitlab.gnome.org/GNOME/gdm/issues
[Translation Project Welcome page]: https://welcome.gnome.org/team/translation/
