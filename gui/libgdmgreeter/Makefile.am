END_OF_LIST =

BUILT_SOURCES = $(END_OF_LIST)
CLEANFILES = $(END_OF_LIST)

AM_CPPFLAGS = \
        -I.                                                                   \
        -I..                                                                  \
        -DG_LOG_DOMAIN=\"GdmGreeter\"                                         \
        $(END_OF_LIST)

lib_LTLIBRARIES =                                                             \
        libgdmgreeter.la                                                      \
        $(END_OF_LIST)

libgdmgreeterdir = $(includedir)/gdm/greeter
libgdmgreeter_HEADERS =                                                       \
        gdm-greeter-client.h                                                  \
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

GdmGreeter-1.0.gir:                                                      \
        $(INTROSPECTION_SCANNER)                                              \
        libgdmgreeter.la                                                      \
        Makefile                                                              \
        $(END_OF_LIST)

GdmGreeter_1_0_gir_SCANNERFLAGS =                                        \
        --warn-all                                                            \
        --namespace=GdmGreeter                                                \
        --identifier-prefix GdmGreeter                                        \
        $(libgdmgreeter_la_CFLAGS)                                            \
        $(END_OF_LIST)

GdmGreeter_1_0_gir_INCLUDES = $(LIBGDMGREETER_GIR_INCLUDES)
GdmGreeter_1_0_gir_LIBS = libgdmgreeter.la
GdmGreeter_1_0_gir_FILES =                                                    \
        $(filter-out %-private.h, $(libgdmgreeter_la_SOURCES))                \
        $(BUILT_SOURCES)                                                      \
        $(END_OF_LIST)

CLEANFILES += $(gir_DATA) $(typelibs_DATA)
endif

MAINTAINERCLEANFILES = Makefile.in
