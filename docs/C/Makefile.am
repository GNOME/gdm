gdm_helpdir = $(datadir)/gnome/help/gdm/C

gdm_help_DATA = 	\
	gdm.html

SGML_FILES = gdm.sgml

EXTRA_DIST = \
	$(SGML_FILES)		

all: 

gdm.html: gdm/index.html
	-cd $(srcdir) && cp gdm/index.html gdm.html

gdm/index.html: $(SGML_FILES)
	-cd $(srcdir) && db2html gdm.sgml

dist-hook:
	mkdir $(distdir)/gdm
	-cp $(srcdir)/gdm/*.html gdm/*.css $(distdir)/gdm
	-cp $(srcdir)/gdm.html $(distdir)
	-cp $(srcdir)/gdm.html $(distdir)/gdm/index.html

install-data-local: gdm.html
	$(mkinstalldirs) $(DESTDIR)$(gdm_helpdir)/images
	-for file in $(srcdir)/gdm/*.html $(srcdir)/gdm/*.css; do \
	basefile=`basename $$file`; \
	$(INSTALL_DATA) $$file $(DESTDIR)$(gdm_helpdir)/$$basefile; \
	done
	$(INSTALL_DATA) $(srcdir)/gdm.html $(DESTDIR)$(gdm_helpdir)/index.html

gdm.ps: $(SGML_FILES)
	db2ps $<

gdm.rtf: $(SGML_FILES)
	-db2rtf $<

