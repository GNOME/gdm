NULL =

INCLUDES = \
	-I.						\
	-I..						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(UTILS_CFLAGS)					\
	$(NULL)

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

sbin_SCRIPTS = 			\
	gdm-stop		\
	gdm-restart		\
	gdm-safe-restart	\
	$(NULL)

EXTRA_DIST = 			\
	gdm-stop.in		\
	gdm-restart.in		\
	gdm-safe-restart.in	\
	$(NULL)

if DMX_SUPPORT
bin_PROGRAMS = gdm-dmx-reconnect-proxy
endif

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

CLEANFILES = 			\
	$(NULL)

DISTCLEANFILES = 		\
	gdm-stop		\
	gdm-restart		\
	gdm-safe-restart	\
	$(NULL)

