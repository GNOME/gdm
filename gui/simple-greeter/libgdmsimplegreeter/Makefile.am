NULL =

AM_CPPFLAGS = \
	-I.					\
	-I..					\
	-I$(top_srcdir)/common			\
	-DBINDIR=\"$(bindir)\"			\
	-DDATADIR=\"$(datadir)\"		\
	-DLIBDIR=\"$(libdir)\"			\
	-DLIBEXECDIR=\"$(libexecdir)\"		\
	-DLOGDIR=\"$(logdir)\"			\
	-DPIXMAPDIR=\"$(pixmapdir)\"		\
	-DSBINDIR=\"$(sbindir)\"		\
	$(GTK_CFLAGS)				\
	$(NULL)

lib_LTLIBRARIES = 			\
	libgdmsimplegreeter.la		\
	$(NULL)

libgdmsimplegreeter_la_SOURCES =		\
	gdm-login-extension.h			\
	gdm-login-extension.c			\
	$(NULL)

libgdmsimplegreeter_la_LIBADD =			\
	$(GTK_LIBS)				\
	$(top_builddir)/common/libgdmcommon.la	\
	$(NULL)

libgdmsimplegreeter_la_LDFLAGS = 		\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-no-undefined				\
	$(NULL)

headersdir = $(includedir)/gdm/simple-greeter
headers_HEADERS = gdm-login-extension.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gdmsimplegreeter.pc

EXTRA_DIST = gdmsimplegreeter.pc
MAINTAINERCLEANFILES = Makefile.in
