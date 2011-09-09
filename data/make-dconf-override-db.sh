#!/bin/bash

set -e

export TMPDIR=$(mktemp -d --tmpdir="$PWD")
export XDG_CONFIG_HOME="$TMPDIR/config"
export XDG_CACHE_HOME="$TMPDIR/cache"
mkdir -p $XDG_CONFIG_HOME $XDG_CACHE_HOME

eval `dbus-launch --sh-syntax`

trap 'rm -rf $TMPDIR; kill $DBUS_SESSION_BUS_PID' ERR

gsettings set org.gnome.power-manager show-actions false || :

gsettings set org.gnome.desktop.a11y.keyboard enable true
gsettings set org.gnome.desktop.background show-desktop-icons false
gsettings set org.gnome.desktop.default-applications.terminal exec '"/bin/true"'
gsettings set org.gnome.desktop.interface toolkit-accessibility true

gsettings set org.gnome.desktop.lockdown disable-application-handlers true
gsettings set org.gnome.desktop.lockdown disable-command-line true
gsettings set org.gnome.desktop.lockdown disable-lock-screen true
gsettings set org.gnome.desktop.lockdown disable-print-setup true
gsettings set org.gnome.desktop.lockdown disable-printing true
gsettings set org.gnome.desktop.lockdown disable-save-to-disk true

gsettings set org.gnome.desktop.sound event-sounds true

gsettings set org.gnome.settings-daemon.plugins.media-keys eject '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys calculator '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys email '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys help '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys home '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys media '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys next '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys pause '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys play '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys previous '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys screensaver '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys search '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys stop '""'
gsettings set org.gnome.settings-daemon.plugins.media-keys www '""'

gsettings list-schemas | egrep '^org\.gnome\.settings-daemon\.plugins\.' | while read schema; do
    gsettings set $schema active false
done

gsettings set org.gnome.settings-daemon.plugins.a11y-keyboard active true
gsettings set org.gnome.settings-daemon.plugins.background active true
gsettings set org.gnome.settings-daemon.plugins.cursor active true
gsettings set org.gnome.settings-daemon.plugins.media-keys active true
gsettings set org.gnome.settings-daemon.plugins.orientation active true
gsettings set org.gnome.settings-daemon.plugins.power active true
gsettings set org.gnome.settings-daemon.plugins.sound active true
gsettings set org.gnome.settings-daemon.plugins.xrandr active true
gsettings set org.gnome.settings-daemon.plugins.xsettings active true

mv $XDG_CONFIG_HOME/dconf/user dconf-override-db
rm -rf $TMPDIR

kill $DBUS_SESSION_BUS_PID
