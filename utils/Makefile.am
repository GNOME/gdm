NULL =

AM_CPPFLAGS = \
	-I$(srcdir)					\
	-I$(builddir)					\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/common				\
	-I$(top_builddir)/common			\
	-DDATADIR=\"$(datadir)\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 		\
	-DGDM_SCREENSHOT_DIR=\""$(GDM_SCREENSHOT_DIR)"\"\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(UTILS_CFLAGS)					\
	$(CANBERRA_GTK_CFLAGS)				\
	$(GTK_CFLAGS)					\
	$(XLIB_CFLAGS)					\
	$(SYSTEMD_CFLAGS)				\
	$(COMMON_CFLAGS)				\
	$(NULL)

edit = sed \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@GDM_PID_FILE[@]|$(GDM_PID_FILE)|g'

bin_PROGRAMS = \
	gdmflexiserver		\
	gdm-screenshot		\
	$(NULL)

libexec_PROGRAMS = \
	gdm-x-session		\
	$(NULL)

gdm_x_session_LDADD =		\
	$(top_builddir)/common/libgdmcommon.la	\
	$(GTK_LIBS)		\
	$(COMMON_LIBS)		\
	$(SYSTEMD_LIBS)         \
	$(XLIB_LIBS)		\
	$(NULL)

gdm_x_session_SOURCES =	\
	gdm-x-session.c	\
	$(NULL)

gdmflexiserver_LDADD =		\
	$(top_builddir)/common/libgdmcommon.la	\
	$(GTK_LIBS)		\
	$(COMMON_LIBS)		\
	$(SYSTEMD_LIBS)         \
	$(NULL)

gdm_screenshot_SOURCES =	\
	gdm-screenshot.c	\
	$(NULL)

gdm_screenshot_LDADD =		\
	$(GTK_LIBS)		\
	$(CANBERRA_GTK_LIBS)	\
	$(XLIB_LIBS)		\
	$(COMMON_LIBS)		\
	$(NULL)

CLEANFILES = 			\
	$(NULL)

DISTCLEANFILES = 		\
	$(NULL)
