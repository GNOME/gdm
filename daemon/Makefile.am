NULL =

AM_CPPFLAGS = \
	-I.						\
	-I..						\
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/pam-extensions			\
	-I$(top_builddir)/common			\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DDMCONFDIR=\"$(dmconfdir)\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"			\
	-DLIBDIR=\"$(libdir)\"				\
	-DLIBEXECDIR=\"$(libexecdir)\"			\
	-DLOCALSTATEDIR=\"$(localstatedir)\"		\
	-DLOGDIR=\"$(logdir)\"				\
	-DSBINDIR=\"$(sbindir)\"			\
	-DSYSCONFDIR=\"$(sysconfdir)\"			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DGDM_RUN_DIR=\"$(GDM_RUN_DIR)\"		\
	-DGDM_XAUTH_DIR=\"$(GDM_XAUTH_DIR)\"		\
	-DGDM_SCREENSHOT_DIR=\"$(GDM_SCREENSHOT_DIR)\"		\
	-DGDM_CACHE_DIR=\""$(localstatedir)/cache/gdm"\"	\
	-DGDM_SESSION_DEFAULT_PATH=\"$(GDM_SESSION_DEFAULT_PATH)\" \
	$(DISABLE_DEPRECATED_CFLAGS)			\
	$(DAEMON_CFLAGS)				\
	$(XLIB_CFLAGS)					\
	$(WARN_CFLAGS)					\
	$(DEBUG_CFLAGS)					\
	$(SYSTEMD_CFLAGS)				\
	$(JOURNALD_CFLAGS)				\
	$(LIBSELINUX_CFLAGS)	 			\
	-DLANG_CONFIG_FILE=\"$(LANG_CONFIG_FILE)\"	\
	$(NULL)

BUILT_SOURCES =					\
	gdm-display-glue.h			\
	gdm-manager-glue.h			\
	gdm-local-display-glue.h		\
	gdm-local-display-factory-glue.h	\
	gdm-session-glue.h			\
	gdm-session-worker-glue.h		\
	gdm-session-enum-types.h		\
	$(NULL)

gdm-session-enum-types.h: gdm-session-enum-types.h.in gdm-session.h
	$(AM_V_GEN) glib-mkenums --template $^ > $@

gdm-session-enum-types.c: gdm-session-enum-types.c.in gdm-session.h
	$(AM_V_GEN) glib-mkenums --template $^ > $@

gdm-display-glue.c gdm-display-glue.h: gdm-display.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace=GdmDBus					\
		--interface-prefix=org.gnome.DisplayManager		\
		--generate-c-code=gdm-display-glue			\
		$(srcdir)/gdm-display.xml

gdm-local-display-glue.c gdm-local-display-glue.h: gdm-local-display.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace=GdmDBus					\
		--interface-prefix=org.gnome.DisplayManager		\
		--generate-c-code=gdm-local-display-glue		\
		$(srcdir)/gdm-local-display.xml

gdm-local-display-factory-glue.c gdm-local-display-factory-glue.h : gdm-local-display-factory.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace=GdmDBus					\
		--interface-prefix=org.gnome.DisplayManager		\
		--generate-c-code=gdm-local-display-factory-glue	\
		$(srcdir)/gdm-local-display-factory.xml

gdm-manager-glue.c gdm-manager-glue.h : gdm-manager.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace=GdmDBus					\
		--interface-prefix=org.gnome.DisplayManager		\
		--generate-c-code=gdm-manager-glue			\
		$(srcdir)/gdm-manager.xml

gdm-session-glue.c gdm-session-glue.h : gdm-session.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace=GdmDBus					\
		--interface-prefix=org.gnome.DisplayManager		\
		--generate-c-code=gdm-session-glue			\
		$(srcdir)/gdm-session.xml

gdm-session-worker-glue.c gdm-session-worker-glue.h : gdm-session-worker.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace=GdmDBus					\
		--interface-prefix=org.gnome.DisplayManager		\
		--generate-c-code=gdm-session-worker-glue		\
		$(srcdir)/gdm-session-worker.xml

noinst_PROGRAMS = 		\
	test-session-client	\
	$(NULL)

test_session_client_SOURCES = \
	test-session-client.c	\
	$(NULL)

nodist_test_session_client_SOURCES =	\
	gdm-session-glue.h		\
	gdm-session-glue.c		\
	gdm-manager-glue.h		\
	gdm-manager-glue.c		\
	$(NULL)

test_session_client_LDADD =	\
	$(DAEMON_LIBS)		\
	$(NULL)

libexec_PROGRAMS = 			\
	gdm-session-worker		\
	gdm-wayland-session		\
	gdm-x-session			\
	$(NULL)

gdm_session_worker_SOURCES = 			\
	session-worker-main.c 			\
	gdm-session.c				\
	gdm-session.h				\
	gdm-session-settings.h			\
	gdm-session-settings.c			\
	gdm-session-auditor.h			\
	gdm-session-auditor.c			\
	gdm-session-record.c			\
	gdm-session-record.h			\
	gdm-session-worker.h			\
	gdm-session-worker.c			\
	gdm-session-worker-job.c		\
	gdm-session-worker-common.c		\
	gdm-session-worker-common.h		\
	gdm-dbus-util.c				\
	gdm-dbus-util.h				\
	$(NULL)

if SUPPORTS_PAM_EXTENSIONS
gdm_session_worker_SOURCES += $(top_srcdir)/pam-extensions/gdm-pam-extensions.h
endif

nodist_gdm_session_worker_SOURCES =		\
	gdm-session-glue.h			\
	gdm-session-glue.c			\
	gdm-session-worker-glue.c		\
	gdm-session-worker-glue.h		\
	gdm-session-enum-types.c		\
	gdm-session-enum-types.h		\
	$(NULL)

gdm_wayland_session_LDADD =		\
	$(top_builddir)/common/libgdmcommon.la	\
	$(GTK_LIBS)		\
	$(COMMON_LIBS)		\
	$(SYSTEMD_LIBS)         \
	$(NULL)

gdm_wayland_session_SOURCES =	\
	gdm-manager-glue.h	\
	gdm-manager-glue.c	\
	gdm-wayland-session.c	\
	$(NULL)

gdm_x_session_LDADD =		\
	$(top_builddir)/common/libgdmcommon.la	\
	$(GTK_LIBS)		\
	$(COMMON_LIBS)		\
	$(SYSTEMD_LIBS)         \
	$(XLIB_LIBS)		\
	$(NULL)

gdm_x_session_SOURCES =	\
	gdm-manager-glue.h	\
	gdm-manager-glue.c	\
	gdm-x-session.c	\
	$(NULL)

if HAVE_LIBAUDIT
gdm_session_worker_SOURCES += gdm-session-linux-auditor.h       \
	                      gdm-session-linux-auditor.c
endif

if HAVE_ADT
gdm_session_worker_SOURCES += gdm-session-solaris-auditor.h     \
	                      gdm-session-solaris-auditor.c
endif

gdm_session_worker_LDADD = 			\
	$(top_builddir)/common/libgdmcommon.la	\
	$(DAEMON_LIBS)				\
	$(SYSTEMD_LIBS) 			\
	$(JOURNALD_LIBS) 			\
	$(LIBSELINUX_LIBS) 			\
	$(EXTRA_DAEMON_LIBS)			\
	$(XLIB_LIBS)				\
	$(PAM_LIBS)				\
	$(NULL)

sbin_PROGRAMS = 			\
	gdm				\
	$(NULL)

gdm_SOURCES = 			\
	main.c				\
	gdm-display-access-file.c	\
	gdm-display-access-file.h	\
	gdm-display-store.c		\
	gdm-display-store.h		\
	gdm-display-factory.c		\
	gdm-display-factory.h		\
	gdm-local-display-factory.c	\
	gdm-local-display-factory.h	\
	gdm-display.c			\
	gdm-display.h			\
	gdm-legacy-display.c		\
	gdm-legacy-display.h		\
	gdm-local-display.c		\
	gdm-local-display.h		\
	gdm-launch-environment.c	\
	gdm-launch-environment.h	\
	gdm-manager.c			\
	gdm-manager.h			\
	gdm-server.c			\
	gdm-server.h			\
	gdm-session.c			\
	gdm-session.h			\
	gdm-session-record.c		\
	gdm-session-record.h		\
	gdm-session-worker-common.c	\
	gdm-session-worker-common.h	\
	gdm-session-worker-job.c	\
	gdm-session-worker-job.h	\
	gdm-dbus-util.c			\
	gdm-dbus-util.h			\
	$(NULL)

nodist_gdm_SOURCES = 			\
	gdm-display-glue.h			\
	gdm-display-glue.c			\
	gdm-local-display-factory-glue.h	\
	gdm-local-display-factory-glue.c	\
	gdm-manager-glue.h			\
	gdm-manager-glue.c			\
	gdm-local-display-glue.h		\
	gdm-local-display-glue.c		\
	gdm-session-glue.h			\
	gdm-session-glue.c			\
	gdm-session-worker-glue.c		\
	gdm-session-worker-glue.h		\
	gdm-session-enum-types.c		\
	gdm-session-enum-types.h		\
	$(NULL)

XDMCP_SOURCES =				\
	gdm-xdmcp-display-factory.c	\
	gdm-xdmcp-display-factory.h	\
	gdm-xdmcp-display.c		\
	gdm-xdmcp-display.h		\
	gdm-xdmcp-chooser-display.c	\
	gdm-xdmcp-chooser-display.h	\
	$(NULL)

if XDMCP_SUPPORT
gdm_SOURCES += $(XDMCP_SOURCES)
endif

EXTRA_gdm_SOURCES = 	\
	$(XDMCP_SOURCES)	\
	$(NULL)

gdm_LDADD = \
	$(top_builddir)/common/libgdmcommon.la	\
	$(XLIB_LIBS)				\
	$(DAEMON_LIBS)				\
	$(XDMCP_LIBS)                           \
	$(LIBWRAP_LIBS)                         \
	$(SYSTEMD_LIBS)				\
	$(JOURNALD_LIBS)				\
	$(EXTRA_DAEMON_LIBS)			\
	$(NULL)

CLEANFILES =					\
	gdm-display-glue.c			\
	gdm-local-display-factory-glue.c	\
	gdm-manager-glue.c			\
	gdm-session-glue.c			\
	gdm-session-worker-glue.c		\
	gdm-session-enum-types.c		\
	gdm-local-display-glue.c		\
	$(BUILT_SOURCES)			\
	$(NULL)

EXTRA_DIST = 				\
	gdm-manager.xml			\
	gdm-session-worker.xml		\
	gdm-session.xml			\
	gdm-display.xml			\
	gdm-local-display.xml		\
	gdm-local-display-factory.xml	\
	gdm-session-enum-types.c.in	\
	gdm-session-enum-types.h.in	\
	$(NULL)
