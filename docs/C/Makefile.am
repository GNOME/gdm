gdm_helpdir = $(datadir)/gnome/help/gdm/C

gdm_help_DATA = 	\
	gdm.html

SGML_FILES = gdm.sgml

EXTRA_DIST = \
	$(SGML_FILES)		

all: 

gdm.html: gdm/gdm.html
	-cd $(srcdir) && cp gdm/gdm.html .

gdm/gdm.html: $(SGML_FILES)
	-cd $(srcdir) && db2html gdm.sgml

dist-hook:
	mkdir $(distdir)/gdm
	-cp $(srcdir)/gdm/*.html gdm/*.css $(distdir)/gdm
	-cp $(srcdir)/gdm.html $(distdir)
	-cp $(srcdir)/gdm.html $(distdir)/gdm/index.html

install-data-local: gdm.html
	$(mkinstalldirs) $(gdm_helpdir)/images
	-for file in $(srcdir)/gdm/*.html $(srcdir)/gdm/*.css; do \
	basefile=`basename $$file`; \
	$(INSTALL_DATA) $$file $(gdm_helpdir)/$$basefile; \
	done

gdm.ps: $(SGML_FILES)
	db2ps $<

gdm.rtf: $(SGML_FILES)
	-db2rtf $<

