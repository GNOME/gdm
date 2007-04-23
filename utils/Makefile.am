NULL =

DEFS = @DEFS@ -DGDM_CONFIG_FILE=\"$(gdmconfdir)/gdm.conf\" \
	-DGDM_FACTORY_CONFIG_FILE=\"$(gdmconfdir)/factory-gdm.conf\" \
	-DGDM_GLADE_DIR=\"@datadir@/gdm\"

INCLUDES = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/daemon				\
	-I$(top_srcdir)/vicious-extensions		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(UTILS_CFLAGS)

edit = sed \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@GDM_PID_FILE[@]|$(GDM_PID_FILE)|g'

gdm-stop: gdm-stop.in Makefile
	$(edit) $< >$@
gdm-restart: gdm-restart.in Makefile
	$(edit) $< >$@
gdm-safe-restart: gdm-safe-restart.in Makefile
	$(edit) $< >$@
gdmsetup: gdmsetup-security.in
	$(edit) <gdmsetup-security.in >gdmsetup


sbin_SCRIPTS = 			\
	gdm-stop		\
	gdm-restart		\
	gdm-safe-restart	\
	$(NULL)

EXTRA_DIST = 			\
	gdm-ssh-session		\
	gdm-stop.in		\
	gdm-restart.in		\
	gdm-safe-restart.in	\
	gdmsetup-pam		\
	gdmsetup-security.in	\
	$(NULL)

libexec_SCRIPTS = \
	@GDMSSHSESSION@

libexec_PROGRAMS = \
	@GDMASKPASS@	\
	@GDMOPEN@	\
	@GDMPREFETCH@	\
	gdmtranslate

if DMX_SUPPORT
bin_PROGRAMS = gdm-dmx-reconnect-proxy
endif

EXTRA_SCRIPTS = gdm-ssh-session
EXTRA_PROGRAMS = gdmaskpass gdmopen gdmprefetch

gdmaskpass_SOURCES = \
	gdmaskpass.c

gdmtranslate_SOURCES = \
	gdmtranslate.c

gdmopen_SOURCES = \
	gdmopen.c

gdmprefetch_SOURCES = \
	gdmprefetch.c

gdmaskpass_LDADD = \
	$(INTLLIBS)		\
	-lpam			\
	$(EXTRA_GDMASKPASS_LIBS)

gdmopen_LDADD = \
	$(INTLLIBS)

gdmtranslate_LDADD = \
	$(INTLLIBS)

if DMX_SUPPORT
gdm_dmx_reconnect_proxy_SOURCES = \
	gdm-dmx-reconnect-proxy.c

gdm_dmx_reconnect_proxy_LDADD = \
	$(GLIB_LIBS) \
        $(X_EXTRA_LIBS) \
        $(X_LIBS) \
	-lX11 \
	$(DMX_LIBS)
endif

if CONSOLE_HELPER
cappsdir = $(PAM_PREFIX)/security/console.apps
capps_DATA = gdmsetup

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(PAM_PREFIX)/pam.d
	$(INSTALL_DATA) $(srcdir)/gdmsetup-pam $(DESTDIR)$(PAM_PREFIX)/pam.d/gdmsetup
else
install-data-local:
endif

CLEANFILES = 			\
	gdmsetup-security	\
	$(NULL)

DISTCLEANFILES = 		\
	gdmsetup		\
	gdmstop			\
	gdm-restart		\
	gdm-safe-restart	\
	$(NULL)

