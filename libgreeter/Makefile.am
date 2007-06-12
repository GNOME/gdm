NULL =

noinst_LTLIBRARIES = libgreeter.la

libgreeter_la_SOURCES = 		\
	gdm-greeter.c			\
	gdm-greeter.h			\
	gdmwm.c				\
	gdmwm.h				\
	gdmsession.c			\
	gdmsession.h			\
	gdmlanguages.c			\
	gdmlanguages.h			\
	$(NULL)

libgreeter_la_CPPFLAGS = 		\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(NULL)

libgreeter_la_CFLAGS =			\
	$(DBUS_CFLAGS)			\
	$(GUI_CFLAGS)			\
	-I$(top_srcdir)/common/		\
	$(NULL)

libgreeter_la_LIBADD =			\
	$(top_srcdir)/common/libgdmcommon.la	\
	$(NULL)

libgreeter_la_LDFLAGS = 		\
	-export-dynamic 		\
	$(NULL)
