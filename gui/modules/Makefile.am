## Process this file with automake to produce makefile.in

INCLUDES = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/vicious-extensions		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(GUI_CFLAGS)

#
#	-DG_DISABLE_DEPRECATED				\
#	-DGDK_DISABLE_DEPRECATED			\
#	-DGDK_PIXBUF_DISABLE_DEPRECATED			\
#	-DGTK_DISABLE_DEPRECATED			\
#	-DGNOME_DISABLE_DEPRECATED			\
#

libkeymouselistener_la_SOURCES = \
	keymouselistener.c

libkeymouselistener_la_LDFLAGS =	\
	-rpath $(moduledir) -module -avoid-version \
	$(GUI_LIBS)	\
	$(X_LIBS)	\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui

libdwellmouselistener_la_SOURCES = \
	dwellmouselistener.c

libdwellmouselistener_la_LDFLAGS =	\
	-rpath $(moduledir) -module -avoid-version \
	$(GUI_LIBS)	\
	$(X_LIBS)	\
	-L$(top_builddir)/vicious-extensions	\
	-lviciousui

moduledir = $(libdir)/gtk-2.0/modules

module_LTLIBRARIES = 		\
	libkeymouselistener.la  \
	libdwellmouselistener.la

moduleconfdir = $(sysconfdir)/gdm/modules

noinst_DATA = \
	AccessKeyMouseEvents \
	AccessDwellMouseEvents

EXTRA_DIST = \
	AccessKeyMouseEvents.in \
	AccessDwellMouseEvents.in

uninstall-hook: 
	rm -f $(DESTDIR)$(moduleconfdir)/AccessKeyMouseEvents \
	$(DESTDIR)$(moduleconfdir)/factory-AccessKeyMouseEvents \
	$(DESTDIR)$(moduleconfdir)/AccessDwellMouseEvents \
	$(DESTDIR)$(moduleconfdir)/factory-AccessDwellMouseEvents

install-data-hook: AccessKeyMouseEvents AccessDwellMouseEvents
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
	-if cmp -s $(DESTDIR)$(moduleconfdir)/AccessDwellMouseEvents $(DESTDIR)$(moduleconfdir)/factory-AccessDwellMouseEvents 2>&1 /dev/null ; then \
		cp -f $(DESTDIR)$(moduleconfdir)/AccessDwellMouseEvents $(DESTDIR)$(moduleconfdir)/AccessDwellMouseEvents.orig; \
		$(INSTALL_DATA) AccessDwellMouseEvents $(DESTDIR)$(moduleconfdir)/AccessDwellMouseEvents; \
	fi
	if test '!' -f $(DESTDIR)$(moduleconfdir)/AccessDwellMouseEvents; then \
		$(INSTALL_DATA) AccessDwellMouseEvents $(DESTDIR)$(moduleconfdir)/AccessDwellMouseEvents; \
	fi
	$(INSTALL_DATA) AccessDwellMouseEvents $(DESTDIR)$(moduleconfdir)/factory-AccessDwellMouseEvents
