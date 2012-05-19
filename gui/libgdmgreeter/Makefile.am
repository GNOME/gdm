END_OF_LIST =

BUILT_SOURCES = gdm-manager-glue.h gdm-client-glue.h
CLEANFILES =                    \
        $(BUILT_SOURCES)        \
        gdm-manager-glue.c      \
        gdm-client-glue.c      \
        $(END_OF_LIST)

gdm-manager-glue.c gdm-manager-glue.h : $(top_srcdir)/daemon/gdm-manager.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace Gdm				\
		--interface-prefix=org.gnome.DisplayManager		\
		--generate-c-code=gdm-manager-glue			\
		--annotate "org.gnome.DisplayManager.Manager"		\
		           "org.gtk.GDBus.C.Name" Manager		\
		$(top_srcdir)/daemon/gdm-manager.xml

gdm-client-glue.c gdm-client-glue.h : $(top_srcdir)/daemon/gdm-session.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace Gdm				\
		--interface-prefix=org.gnome.DisplayManager		\
		--generate-c-code=gdm-client-glue			\
		--annotate "org.gnome.DisplayManager.UserVerifier"	\
		           "org.gtk.GDBus.C.Name" UserVerifier	\
		--annotate "org.gnome.DisplayManager.Greeter"		\
		           "org.gtk.GDBus.C.Name" Greeter		\
		--annotate "org.gnome.DisplayManager.RemoteGreeter"	\
		           "org.gtk.GDBus.C.Name" RemoteGreeter		\
		--annotate "org.gnome.DisplayManager.Chooser"		\
		           "org.gtk.GDBus.C.Name" Chooser		\
		$(top_srcdir)/daemon/gdm-session.xml

AM_CPPFLAGS = \
        -I.                                                                   \
        -I..                                                                  \
        -I$(srcdir)                                                           \
        -DG_LOG_DOMAIN=\"Gdm\"                                         \
        -DDMCONFDIR=\""$(dmconfdir)"\"                                        \
        -DDATADIR=\""$(datadir)"\"                                            \
        $(END_OF_LIST)

lib_LTLIBRARIES =                                                             \
        libgdmgreeter.la                                                      \
        $(END_OF_LIST)

libgdmgreeterdir = $(includedir)/gdm
libgdmgreeter_HEADERS =                                                       \
        gdm-greeter-client.h                                                   \
        gdm-greeter-sessions.h                                                 \
        $(END_OF_LIST)

libgdmgreeter_la_CFLAGS =                                                     \
        $(LIBGDMGREETER_CFLAGS)                                               \
        $(END_OF_LIST)
libgdmgreeter_la_LDFLAGS =                                                    \
        -export-symbols-regex '^[^_].*'                                       \
        -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)                  \
        -no-undefined                                                         \
        $(END_OF_LIST)

libgdmgreeter_la_LIBADD =                                                     \
        $(LIBGDMGREETER_LIBS)                                                 \
        $(END_OF_LIST)

libgdmgreeter_la_SOURCES =                                                    \
        $(libgdmgreeter_HEADERS)                                              \
        gdm-greeter-client.c                                                  \
        gdm-greeter-sessions.c                                                \
        $(END_OF_LIST)

nodist_libgdmgreeter_la_SOURCES = 	\
	gdm-client-glue.c		\
	gdm-client-glue.h		\
	gdm-manager-glue.c		\
	gdm-manager-glue.h		\
	$(END_OF_LIST)

pkgconfigdir = $(libdir)/pkgconfig
dist_pkgconfig_DATA = gdmgreeter.pc

-include $(INTROSPECTION_MAKEFILE)

if HAVE_INTROSPECTION
girdir = $(datadir)/gir-1.0
gir_DATA = GdmGreeter-1.0.gir
typelibsdir = $(libdir)/girepository-1.0
typelibs_DATA = $(gir_DATA:.gir=.typelib)
INTROSPECTION_GIRS = $(gir_DATA)

GdmGreeter-1.0.gir:                                                           \
        $(INTROSPECTION_SCANNER)                                              \
        libgdmgreeter.la                                                      \
        Makefile                                                              \
        $(END_OF_LIST)

GdmGreeter_1_0_gir_SCANNERFLAGS =                                             \
        --warn-all                                                            \
        --namespace=GdmGreeter                                                \
        --identifier-prefix Gdm                                               \
        $(AM_CPPFLAGS)                                                        \
        $(libgdmgreeter_la_CFLAGS)                                            \
        $(END_OF_LIST)

GdmGreeter_1_0_gir_INCLUDES = $(LIBGDMGREETER_GIR_INCLUDES)
GdmGreeter_1_0_gir_LIBS = libgdmgreeter.la
GdmGreeter_1_0_gir_FILES =                                                    \
        $(filter-out %-private.h, $(libgdmgreeter_la_SOURCES))                \
        $(filter-out %-private.h, $(nodist_libgdmgreeter_la_SOURCES))         \
        $(END_OF_LIST)

CLEANFILES += $(gir_DATA) $(typelibs_DATA)
endif

MAINTAINERCLEANFILES = Makefile.in
