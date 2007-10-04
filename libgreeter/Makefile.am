NULL =

noinst_LTLIBRARIES = libgreeter.la

libgreeter_la_SOURCES = 		\
	gdm-greeter.c			\
	gdm-greeter.h			\
	$(NULL)

libgreeter_la_CPPFLAGS = 		\
	$(WARN_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(NULL)

libgreeter_la_CFLAGS =			\
	$(GREETER_CFLAGS)		\
	-I$(top_srcdir)/common/		\
	$(NULL)

libgreeter_la_LIBADD =			\
	$(top_builddir)/common/libgdmcommon.la	\
	$(NULL)

libgreeter_la_LDFLAGS = 		\
	-export-dynamic 		\
	$(NULL)
