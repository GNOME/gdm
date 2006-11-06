## Process this file with automake to produce makefile.in
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

EXTRA_DIST = gdm-ssh-session

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
