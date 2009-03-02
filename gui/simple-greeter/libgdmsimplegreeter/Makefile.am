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
	gdm-task.h				\
	gdm-task.c				\
	gdm-conversation.h			\
	gdm-conversation.c			\
	gdm-greeter-extension.h			\
	gdm-greeter-extension.c			\
	$(NULL)

libgdmsimplegreeter_la_LIBADD =			\
	$(GTK_LIBS)				\
	$(top_builddir)/common/libgdmcommon.la	\
	$(NULL)

libgdmsimplegreeter_la_LDFLAGS = 		\
	-export-symbols-regex '^[^_].*'		\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-no-undefined				\
	$(NULL)

headersdir = $(includedir)/gdm/simple-greeter
headers_HEADERS = gdm-greeter-extension.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gdmsimplegreeter.pc

EXTRA_DIST = gdmsimplegreeter.pc
MAINTAINERCLEANFILES = Makefile.in
