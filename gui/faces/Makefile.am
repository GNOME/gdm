
imagedir = $(datadir)/pixmaps/faces

IMGFILES = bonfire.jpg \
           cdrom.jpg \
           clock.jpg \
           daisy.jpg \
           dice.jpg \
           hollyleaf.jpg \
           lamas.jpg \
           seal.jpg \
           sky.jpg \
           skyline.jpg

EXTRA_DIST = $(IMGFILES)

uninstall-hook:
	rm -f $(DESTDIR)$(imagedir)/bonfire.jpg \
        $(DESTDIR)$(imagedir)/cdrom.jpg \
        $(DESTDIR)$(imagedir)/clock.jpg \
        $(DESTDIR)$(imagedir)/daisy.jpg \
        $(DESTDIR)$(imagedir)/dice.jpg \
        $(DESTDIR)$(imagedir)/hollyleaf.jpg \
        $(DESTDIR)$(imagedir)/lamas.jpg \
        $(DESTDIR)$(imagedir)/seal.jpg \
        $(DESTDIR)$(imagedir)/sky.jpg \
        $(DESTDIR)$(imagedir)/skyline.jpg

install-data-hook: $(IMGFILES)
	if test '!' -d $(DESTDIR)$(imagedir); then \
		$(mkinstalldirs) $(DESTDIR)$(imagedir); \
		chmod 755 $(DESTDIR)$(imagedir); \
	fi
	$(INSTALL_DATA) $(IMGFILES) $(DESTDIR)$(imagedir)


