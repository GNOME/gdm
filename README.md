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

GDM is configured through two main mechanisms: a daemon configuration file
and login screen settings via dconf.

- [Daemon configuration](#daemon-configuration)
- [Login screen settings](#login-screen-settings)

### Daemon configuration

The daemon is configured via `/etc/gdm/custom.conf`, an INI-style key file.
Settings in `/run/gdm/custom.conf` take priority over `/etc/gdm/custom.conf`
and can be used for runtime overrides.

#### [daemon]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `AutomaticLoginEnable` | bool | `false` | Enable automatic login without credentials |
| `AutomaticLogin` | string | | Username for automatic login |
| `TimedLoginEnable` | bool | `false` | Enable timed automatic login |
| `TimedLogin` | string | | Username for timed login |
| `TimedLoginDelay` | int | `30` | Seconds to wait before timed login triggers |
| `InitialSetupEnable` | bool | `true` | Run GNOME Initial Setup on first boot |
| `XorgEnable` | bool | `true` | Allow X11 sessions (requires `x11-support` build option) |
| `RemoteLoginEnable` | bool | `true` | Allow creating remote displays for remote login |

#### [security]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `DisallowTCP` | bool | `true` | Block TCP connections to the X server |

#### [debug]

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `Enable` | bool | `false` | Enable verbose debug logging |

Example `/etc/gdm/custom.conf`:

```ini
[daemon]
AutomaticLoginEnable=true
AutomaticLogin=maria

[debug]
Enable=true
```

### Login screen settings

The login screen appearance and behavior is controlled by GSettings keys
under the `org.gnome.login-screen` schema. These are configured via dconf
by creating a key file in `/etc/dconf/db/gdm.d/` and running `dconf update`.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `enable-switchable-authentication` | bool | `true` | Allow switchable PAM authentication |
| `enable-web-authentication` | bool | `true` | Allow web-based authentication (requires switchable-authentication) |
| `enable-passkey-authentication` | bool | `true` | Allow passkey/FIDO2 authentication (requires switchable-authentication) |
| `enable-fingerprint-authentication` | bool | `true` | Allow fingerprint authentication |
| `enable-smartcard-authentication` | bool | `true` | Allow smartcard authentication |
| `enable-password-authentication` | bool | `true` | Allow password authentication |
| `logo` | string | | Path to branding logo shown on the login screen |
| `disable-user-list` | bool | `false` | Hide the user list, requiring manual username entry |
| `banner-message-enable` | bool | `false` | Show a banner message on the login screen |
| `banner-message-source` | enum | `settings` | Banner source: `settings` or `file` |
| `banner-message-text` | string | | Text of the banner message |
| `banner-message-path` | string | | Path to a text file containing the banner message |
| `disable-restart-buttons` | bool | `false` | Hide restart and shutdown buttons |
| `allowed-failures` | int | `3` | Authentication attempts before returning to user selection |

Example `/etc/dconf/db/gdm.d/01-custom`:

```ini
[org/gnome/login-screen]
banner-message-enable=true
banner-message-text='Authorized use only'
disable-user-list=true
```

After creating or editing the file, run `dconf update` to apply the changes.

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
