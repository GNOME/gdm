NULL =

noinst_LTLIBRARIES = libgreeter.la

libgreeter_la_SOURCES = 		\
	gdm-greeter.c			\
	gdm-greeter.h			\
	$(NULL)

libgreeter_la_CPPFLAGS = 		\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(NULL)

libgreeter_la_CFLAGS =			\
	$(DBUS_CFLAGS)			\
	$(NULL)

libgreeter_la_LIBADD =			\
	$(NULL)

libgreeter_la_LDFLAGS = 		\
	-export-dynamic 		\
	$(NULL)
