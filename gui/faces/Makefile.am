
imagedir = $(datadir)/pixmaps/faces

EXTRA_DIST = bonfire.jpg \
           cdrom.jpg \
           clock.jpg \
           daisy.jpg \
           dice.jpg \
           hollyleaf.jpg \
           lamas.jpg \
           seal.jpg \
           sky.jpg \
           skyline.jpg

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

install-data-hook: $(EXTRA_DIST)
	if test '!' -d $(DESTDIR)$(imagedir); then \
		$(mkinstalldirs) $(DESTDIR)$(imagedir); \
		chmod 755 $(DESTDIR)$(imagedir); \
	fi

	$(INSTALL_DATA) $(srcdir)/bonfire.jpg $(DESTDIR)$(imagedir)
	$(INSTALL_DATA) $(srcdir)/cdrom.jpg $(DESTDIR)$(imagedir)
	$(INSTALL_DATA) $(srcdir)/clock.jpg $(DESTDIR)$(imagedir)
	$(INSTALL_DATA) $(srcdir)/daisy.jpg $(DESTDIR)$(imagedir)
	$(INSTALL_DATA) $(srcdir)/dice.jpg $(DESTDIR)$(imagedir)
	$(INSTALL_DATA) $(srcdir)/hollyleaf.jpg $(DESTDIR)$(imagedir)
	$(INSTALL_DATA) $(srcdir)/lamas.jpg $(DESTDIR)$(imagedir)
	$(INSTALL_DATA) $(srcdir)/seal.jpg $(DESTDIR)$(imagedir)
	$(INSTALL_DATA) $(srcdir)/sky.jpg $(DESTDIR)$(imagedir)
	$(INSTALL_DATA) $(srcdir)/skyline.jpg $(DESTDIR)$(imagedir)

