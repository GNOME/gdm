NULL =

# Note that we have cflags for just about everything even if
# we don't link to everything
INCLUDES = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/common				\
	-DAUTHDIR=\"$(authdir)\"			\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DDMCONFDIR=\"$(dmconfdir)\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"			\
	-DGDMLOCALEDIR=\"$(gdmlocaledir)\"		\
	-DLIBDIR=\"$(libdir)\"				\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-DLOGDIR=\"$(logdir)\"				\
	-DPIXMAPDIR=\"$(pixmapdir)\"			\
	-DSBINDIR=\"$(sbindir)\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DGDM_DEFAULTS_CONF=\"$(GDM_DEFAULTS_CONF)\"	\
	-DGDM_CUSTOM_CONF=\"$(GDM_CUSTOM_CONF)\"	\
	-DGDM_OLD_CONF=\"$(GDM_OLD_CONF)\"		\
	$(GUI_CFLAGS)					\
	$(DAEMON_CFLAGS)				\
	-DLANG_CONFIG_FILE=\"$(LANG_CONFIG_FILE)\"	\
	$(GNOME_INCLUDEDIR)				\
	-DGREETERTHEMEDIR=\""$(datadir)/gdm/themes"\"	\
	$(DBUS_CFLAGS)					\
	$(NULL)

BUILT_SOURCES =					\
	gdm-slave-glue.h			\
	gdm-simple-slave-glue.h			\
	gdm-factory-slave-glue.h		\
	gdm-product-slave-glue.h		\
	gdm-manager-glue.h			\
	gdm-display-glue.h			\
	gdm-xdmcp-display-glue.h		\
	gdm-static-display-glue.h		\
	gdm-static-factory-display-glue.h	\
	gdm-product-display-glue.h		\
	$(NULL)

gdm-manager-glue.h: gdm-manager.xml Makefile.am
	dbus-binding-tool --prefix=gdm_manager --mode=glib-server --output=gdm-manager-glue.h gdm-manager.xml
gdm-slave-glue.h: gdm-slave.xml Makefile.am
	dbus-binding-tool --prefix=gdm_slave --mode=glib-server --output=gdm-slave-glue.h gdm-slave.xml
gdm-simple-slave-glue.h: gdm-simple-slave.xml Makefile.am
	dbus-binding-tool --prefix=gdm_simple_slave --mode=glib-server --output=gdm-simple-slave-glue.h gdm-simple-slave.xml
gdm-factory-slave-glue.h: gdm-factory-slave.xml Makefile.am
	dbus-binding-tool --prefix=gdm_factory_slave --mode=glib-server --output=gdm-factory-slave-glue.h gdm-factory-slave.xml
gdm-product-slave-glue.h: gdm-product-slave.xml Makefile.am
	dbus-binding-tool --prefix=gdm_product_slave --mode=glib-server --output=gdm-product-slave-glue.h gdm-product-slave.xml
gdm-display-glue.h: gdm-display.xml Makefile.am
	dbus-binding-tool --prefix=gdm_display --mode=glib-server --output=gdm-display-glue.h gdm-display.xml
gdm-xdmcp-display-glue.h: gdm-xdmcp-display.xml Makefile.am
	dbus-binding-tool --prefix=gdm_xdmcp_display --mode=glib-server --output=gdm-xdmcp-display-glue.h gdm-xdmcp-display.xml
gdm-static-display-glue.h: gdm-static-display.xml Makefile.am
	dbus-binding-tool --prefix=gdm_static_display --mode=glib-server --output=gdm-static-display-glue.h gdm-static-display.xml
gdm-static-factory-display-glue.h: gdm-static-factory-display.xml Makefile.am
	dbus-binding-tool --prefix=gdm_static_factory_display --mode=glib-server --output=gdm-static-factory-display-glue.h gdm-static-factory-display.xml
gdm-product-display-glue.h: gdm-product-display.xml Makefile.am
	dbus-binding-tool --prefix=gdm_product_display --mode=glib-server --output=gdm-product-display-glue.h gdm-product-display.xml

noinst_PROGRAMS = 		\
	test-session		\
	$(NULL)

test_session_SOURCES = 		\
	test-session.c	 	\
	gdm-session.c		\
	$(NULL)

test_session_LDADD =		\
	$(GLIB_LIBS)		\
	$(GOBJECT_LIBS)		\
	$(PAM_LIBS)	\
	$(NULL)

libexec_PROGRAMS = 			\
	gdm-simple-slave		\
	gdm-factory-slave		\
	gdm-product-slave		\
	$(NULL)

gdm_simple_slave_SOURCES = 		\
	simple-slave-main.c 		\
	gdm-greeter-server.c		\
	gdm-greeter-server.h		\
	gdm-greeter-proxy.c		\
	gdm-greeter-proxy.h		\
	gdm-server.c			\
	gdm-server.h			\
	gdm-session.c			\
	gdm-session.h			\
	gdm-slave.c			\
	gdm-slave.h			\
	gdm-simple-slave.c		\
	gdm-simple-slave.h		\
	fstype.c			\
	filecheck.c			\
	filecheck.h			\
	$(NULL)

gdm_simple_slave_LDFLAGS =		\
	$(PAM_LIBS)			\
	$(NULL)

gdm_simple_slave_LDADD = 			\
	$(DAEMON_LIBS)				\
	$(INTLLIBS)				\
	$(GLIB_LIBS)				\
	$(GOBJECT_LIBS)				\
	$(GDK_LIBS)				\
	$(top_builddir)/common/libgdmcommon.la	\
	$(X_LIBS)				\
	$(XINERAMA_LIBS)			\
	$(DBUS_LIBS)				\
	-lXau					\
	-lX11					\
	-lXext					\
	$(NULL)

gdm_factory_slave_SOURCES = 		\
	factory-slave-main.c 		\
	gdm-greeter-proxy.c		\
	gdm-greeter-proxy.h		\
	gdm-server.c			\
	gdm-server.h			\
	gdm-session.c			\
	gdm-session.h			\
	gdm-slave.c			\
	gdm-slave.h			\
	gdm-factory-slave.c		\
	gdm-factory-slave.h		\
	fstype.c			\
	filecheck.c			\
	filecheck.h			\
	$(NULL)

gdm_factory_slave_LDFLAGS =		\
	$(PAM_LIBS)			\
	$(NULL)

gdm_factory_slave_LDADD = 			\
	$(DAEMON_LIBS)				\
	$(INTLLIBS)				\
	$(GLIB_LIBS)				\
	$(GOBJECT_LIBS)				\
	$(GDK_LIBS)				\
	$(top_builddir)/common/libgdmcommon.la	\
	$(X_LIBS)				\
	$(XINERAMA_LIBS)			\
	$(DBUS_LIBS)				\
	-lXau					\
	-lX11					\
	-lXext					\
	$(NULL)

gdm_product_slave_SOURCES = 		\
	product-slave-main.c 		\
	gdm-greeter-server.c		\
	gdm-greeter-server.h		\
	gdm-server.c			\
	gdm-server.h			\
	gdm-session.c			\
	gdm-session.h			\
	gdm-slave.c			\
	gdm-slave.h			\
	gdm-product-slave.c		\
	gdm-product-slave.h		\
	fstype.c			\
	filecheck.c			\
	filecheck.h			\
	$(NULL)

gdm_product_slave_LDFLAGS =		\
	$(PAM_LIBS)			\
	$(NULL)

gdm_product_slave_LDADD = 			\
	$(DAEMON_LIBS)				\
	$(INTLLIBS)				\
	$(GLIB_LIBS)				\
	$(GOBJECT_LIBS)				\
	$(GDK_LIBS)				\
	$(top_builddir)/common/libgdmcommon.la	\
	$(X_LIBS)				\
	$(XINERAMA_LIBS)			\
	$(DBUS_LIBS)				\
	-lXau					\
	-lX11					\
	-lXext					\
	$(NULL)

sbin_PROGRAMS = 			\
	gdm-binary			\
	$(NULL)

gdm_binary_SOURCES = 			\
	gdm.h 				\
	main.c				\
	gdm-display-store.c		\
	gdm-display-store.h		\
	gdm-display.c			\
	gdm-display.h			\
	gdm-xdmcp-display.c		\
	gdm-xdmcp-display.h		\
	gdm-static-display.c		\
	gdm-static-display.h		\
	gdm-static-factory-display.c	\
	gdm-static-factory-display.h	\
	gdm-product-display.c		\
	gdm-product-display.h		\
	gdm-manager.c			\
	gdm-manager.h			\
	gdm-slave-proxy.c		\
	gdm-slave-proxy.h		\
	gdm-socket-protocol.h		\
	auth.c				\
	auth.h				\
	fstype.c			\
	server.h			\
	choose.c			\
	choose.h			\
	filecheck.c			\
	filecheck.h			\
	$(NULL)

XDMCP_SOURCES =			\
	gdm-xdmcp-manager.c	\
	gdm-xdmcp-manager.h	\
	$(NULL)

if XDMCP_SUPPORT
gdm_binary_SOURCES += $(XDMCP_SOURCES)
endif

EXTRA_gdm_binary_SOURCES = 	\
	$(XDMCP_SOURCES)	\
	$(NULL)

CONSOLE_KIT_SOURCES = 		\
	$(NULL)

EXTRA_gdm_binary_SOURCES += $(CONSOLE_KIT_SOURCES)

# Note that these libs are in LDFLAGS because they should come before
# everything else on the link line as they may override stuff
gdm_binary_LDFLAGS =				\
	$(EXTRA_DAEMON_LIBS)			\
	$(NULL)

gdm_binary_LDADD = \
	$(DAEMON_LIBS)				\
	$(INTLLIBS)				\
	$(GLIB_LIBS)				\
	$(GOBJECT_LIBS)				\
	$(GDK_LIBS)				\
	$(top_builddir)/common/libgdmcommon.a	\
	$(X_LIBS)				\
	$(XINERAMA_LIBS)			\
	$(XDMCP_LIBS)				\
	$(DBUS_LIBS)				\
	-lXau					\
	-lX11					\
	-lXext					\
	$(NULL)

if WITH_CONSOLE_KIT
gdm_binary_SOURCES += $(CONSOLE_KIT_SOURCES)
endif

sbin_SCRIPTS =		\
	gdm		\
	$(NULL)

gdm: $(srcdir)/gdm.in
	sed -e 's,[@]sbindir[@],$(sbindir),g' <$(srcdir)/gdm.in >gdm

CLEANFILES =			\
	gdm			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 				\
	gdm.in				\
	gdm-slave.xml			\
	gdm-simple-slave.xml		\
	gdm-factory-slave.xml		\
	gdm-product-slave.xml		\
	gdm-manager.xml			\
	gdm-display.xml			\
	gdm-xdmcp-display.xml		\
	gdm-static-display.xml		\
	gdm-static-factory-display.xml	\
	gdm-product-display.xml		\
	$(NULL)
