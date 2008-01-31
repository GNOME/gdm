NULL =

INCLUDES = \
	-I$(top_srcdir)/common				\
	-DAUTHDIR=\""$(authdir)"\"		 	\
	-DDATADIR=\""$(datadir)"\"		 	\
	-DGDMCONFDIR=\"$(gdmconfdir)\"			\
	-DDMCONFDIR=\""$(dmconfdir)"\"		 	\
	-DGDMLOCALEDIR=\""$(gdmlocaledir)"\"		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	-DPIXMAPDIR=\""$(pixmapdir)"\"		 	\
	-DAT_SPI_REGISTRYD_DIR="\"$(AT_SPI_REGISTRYD_DIR)\""	\
	$(GTK_CFLAGS)					\
	$(XDMCP_CFLAGS)					\
	$(EXTRA_CHOOSER_CFLAGS)				\
	$(SIMPLE_CHOOSER_CFLAGS)			\
	$(NULL)

libexec_PROGRAMS =			\
	gdm-simple-chooser		\
	$(NULL)

noinst_PROGRAMS = 			\
	test-host-chooser		\
	$(NULL)

gdm_simple_chooser_SOURCES =  		\
	chooser-main.c 			\
	gdm-host-chooser-widget.c	\
	gdm-host-chooser-widget.h	\
	gdm-host-chooser-dialog.c	\
	gdm-host-chooser-dialog.h	\
	gdm-chooser-client.c		\
	gdm-chooser-client.h		\
	gdm-chooser-session.c		\
	gdm-chooser-session.h		\
	$(NULL)

gdm_simple_chooser_LDADD = 		\
	$(GTK_LIBS)			\
	$(EXTRA_CHOOSER_LIBS)   	\
	$(SIMPLE_CHOOSER_LIBS)		\
	$(XDMCP_LIBS)			\
	$(top_builddir)/common/libgdmcommon.la	\
	$(NULL)

test_host_chooser_SOURCES = 		\
	test-host-chooser.c 		\
	gdm-host-chooser-widget.c	\
	gdm-host-chooser-widget.h	\
	gdm-host-chooser-dialog.c	\
	gdm-host-chooser-dialog.h	\
	$(NULL)

test_host_chooser_LDADD =		\
	$(GTK_LIBS)			\
	$(EXTRA_CHOOSER_LIBS)   	\
	$(SIMPLE_CHOOSER_LIBS)		\
	$(XDMCP_LIBS)			\
	$(top_builddir)/common/libgdmcommon.la	\
	$(NULL)

EXTRA_DIST = 			\
	$(NULL)
