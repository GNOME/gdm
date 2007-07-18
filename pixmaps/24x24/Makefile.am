# This file will be processed with automake-1.7 to create Makefile.in

icons = \
	preferences-desktop-locale.png \
	preferences-desktop-remote-desktop.png \
	system-log-out.png \
	system-restart.png \
	system-shut-down.png \
	system-suspend.png \
	user-desktop.png

icon_names = \
	preferences_desktop_locale_24 $(srcdir)/preferences-desktop-locale.png \
	preferences_desktop_remote_desktop_24 $(srcdir)/preferences-desktop-remote-desktop.png \
	system_log_out_24 $(srcdir)/system-log-out.png \
	system_restart_24 $(srcdir)/system-restart.png \
	system_shut_down_24 $(srcdir)/system-shut-down.png \
	system_suspend_24 $(srcdir)/system-suspend.png \
	user_desktop_24 $(srcdir)/user-desktop.png

EXTRA_DIST = $(icons)
BUILT_SOURCES = inlines.h
CLEANFILES = $(BUILT_SOURCES)

inlines.h: $(icons)
	gdk-pixbuf-csource --raw --build-list $(icon_names) > $@

