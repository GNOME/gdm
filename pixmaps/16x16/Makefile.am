# This file will be processed with automake-1.7 to create Makefile.in

appsicondir = \
	$(datadir)/icons/hicolor/16x16/apps
dist_appsicon_DATA = \
	gdm-xnest.png

MAINTAINERCLEANFILES = \
	Makefile.in

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor
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

