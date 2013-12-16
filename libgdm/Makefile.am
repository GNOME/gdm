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
        -I$(srcdir)                                                     \
        -I$(builddir)                                                   \
        -I$(top_srcdir)                                                 \
        -I$(top_builddir)                                               \
        -DG_LOG_DOMAIN=\"Gdm\"                                          \
        -DDMCONFDIR=\""$(dmconfdir)"\"                                        \
        -DDATADIR=\""$(datadir)"\"                                            \
        $(END_OF_LIST)

lib_LTLIBRARIES =                                                             \
        libgdm.la                                                      \
        $(END_OF_LIST)

libgdmdir = $(includedir)/gdm
libgdm_HEADERS =                                                       \
	gdm-client-glue.h                                              \
        gdm-client.h                                                   \
        gdm-sessions.h                                                 \
        gdm-user-switching.h                                           \
        $(END_OF_LIST)

libgdm_la_CFLAGS =                                                     \
        $(LIBGDM_CFLAGS)                                               \
	$(SYSTEMD_CFLAGS)                                              \
        $(END_OF_LIST)
libgdm_la_LDFLAGS =                                                    \
        -export-symbols-regex '^[^_].*'                                       \
        -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)                  \
        -no-undefined                                                         \
        $(END_OF_LIST)

libgdm_la_LIBADD =                                                     \
        $(LIBGDM_LIBS)                                                 \
        $(SYSTEMD_LIBS)                                                \
        $(END_OF_LIST)

libgdm_la_SOURCES =                                                    \
        $(libgdm_HEADERS)                                              \
        gdm-client.c                                                   \
        gdm-sessions.c                                                 \
        gdm-user-switching.c                                           \
        $(END_OF_LIST)

nodist_libgdm_la_SOURCES = 	\
	gdm-client-glue.c		\
	gdm-client-glue.h		\
	gdm-manager-glue.c		\
	gdm-manager-glue.h		\
	$(END_OF_LIST)

pkgconfigdir = $(libdir)/pkgconfig
dist_pkgconfig_DATA = gdm.pc

-include $(INTROSPECTION_MAKEFILE)

if HAVE_INTROSPECTION
girdir = $(datadir)/gir-1.0
gir_DATA = Gdm-1.0.gir
typelibsdir = $(libdir)/girepository-1.0
typelibs_DATA = $(gir_DATA:.gir=.typelib)
INTROSPECTION_GIRS = $(gir_DATA)

Gdm-1.0.gir:                                                                  \
        $(INTROSPECTION_SCANNER)                                              \
        libgdm.la                                                             \
        Makefile                                                              \
        $(END_OF_LIST)

Gdm_1_0_gir_SCANNERFLAGS =                                                    \
        --warn-all                                                            \
        --namespace=Gdm                                                       \
        --identifier-prefix Gdm                                               \
        $(AM_CPPFLAGS)                                                        \
        $(libgdm_la_CFLAGS)                                                   \
        $(END_OF_LIST)

Gdm_1_0_gir_INCLUDES = $(LIBGDM_GIR_INCLUDES)
Gdm_1_0_gir_LIBS = libgdm.la
Gdm_1_0_gir_FILES =                                                    \
        $(filter-out %-private.h, $(libgdm_la_SOURCES))                \
        $(filter-out %-private.h, $(nodist_libgdm_la_SOURCES))         \
        $(END_OF_LIST)

CLEANFILES += $(gir_DATA) $(typelibs_DATA)
endif

MAINTAINERCLEANFILES = Makefile.in
