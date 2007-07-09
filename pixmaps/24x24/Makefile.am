# This file will be processed with automake-1.7 to create Makefile.in

themedir = $(pkgdatadir)/icons/hicolor
size = 24x24

actionsicondir = $(themedir)/$(size)/actions
dist_actionsicon_DATA = \
	system-log-out.png \
	system-restart.png \
	system-shut-down.png \
	system-suspend.png

appsicondir = $(themedir)/$(size)/apps
dist_appsicon_DATA = \
	preferences-desktop-locale.png \
	preferences-desktop-remote-desktop.png

placesicondir = $(themedir)/$(size)/places
dist_placesicon_DATA = \
	user-desktop.png

MAINTAINERCLEANFILES = \
	Makefile.in

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(themedir)
install-data-hook: update-icon-cache
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

