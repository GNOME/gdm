## Process this file with automake to produce makefile.in

INCLUDES = \
	-I.						\
	-I..						\
	-DG_DISABLE_DEPRECATED				\
	-DGDK_DISABLE_DEPRECATED			\
	-DGDK_PIXBUF_DISABLE_DEPRECATED			\
	-DGTK_DISABLE_DEPRECATED			\
	-DGNOME_DISABLE_DEPRECATED			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(GUI_CFLAGS)

libkeymouselistener_la_SOURCES = \
	keymouselistener.c

libkeymouselistener_la_LDFLAGS =	\
	-rpath $(moduledir) -module -avoid-version \
	$(GUI_LIBS)	\
	$(X_LIBS)

moduledir = $(libdir)/gtk-2.0/modules

module_LTLIBRARIES = 		\
	libkeymouselistener.la

moduleconfdir = $(sysconfdir)/gdm/modules

noinst_DATA = AccessKeyMouseEvents

EXTRA_DIST = \
	AccessKeyMouseEvents.in

install-data-hook: AccessKeyMouseEvents
	if test '!' -d $(DESTDIR)$(moduleconfdir); then \
		$(mkinstalldirs) $(DESTDIR)$(moduleconfdir); \
		chmod 755 $(DESTDIR)$(moduleconfdir); \
	fi
	-if cmp -s $(DESTDIR)$(moduleconfdir)/AccessKeyMouseEvents $(DESTDIR)$(moduleconfdir)/factory-AccessKeyMouseEvents 2>&1 /dev/null ; then \
		cp -f $(DESTDIR)$(moduleconfdir)/AccessKeyMouseEvents $(DESTDIR)$(moduleconfdir)/AccessKeyMouseEvents.orig; \
		$(INSTALL_DATA) AccessKeyMouseEvents $(DESTDIR)$(moduleconfdir)/AccessKeyMouseEvents; \
	fi
	if test '!' -f $(DESTDIR)$(moduleconfdir)/AccessKeyMouseEvents; then \
		$(INSTALL_DATA) AccessKeyMouseEvents $(DESTDIR)$(moduleconfdir)/AccessKeyMouseEvents; \
	fi
	$(INSTALL_DATA) AccessKeyMouseEvents $(DESTDIR)$(moduleconfdir)/factory-AccessKeyMouseEvents
