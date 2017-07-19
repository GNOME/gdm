NULL =

AM_CPPFLAGS = \
        -I$(srcdir)                                                     \
        -I$(builddir)                                                   \
        -I$(top_srcdir)                                                 \
        -I$(top_builddir)                                               \
        -DG_LOG_DOMAIN=\"Gdm\"                                          \
        -DDMCONFDIR=\""$(dmconfdir)"\"                                  \
        -DDATADIR=\""$(datadir)"\"                                      \
        $(NULL)

if SUPPORTS_PAM_EXTENSIONS
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gdm-pam-extensions.pc

pam_extensions_includedir = $(includedir)/gdm
pam_extensions_include_HEADERS = gdm-pam-extensions.h

pam_gdm_testlist_la_SOURCES = \
	pam_gdm_testlist.c \
	$(NULL)

pam_gdm_testlist_la_CFLAGS = \
	$(PAM_CFLAGS) \
	-DGDM_IN_TREE_BUILD=1 \
	$(NULL)

pam_gdm_testlist_la_LDFLAGS = \
	-module \
	-export-dynamic \
	-avoid-version \
	-shared \
	$(NULL)

pam_gdm_testlist_la_LIBADD = \
	$(PAM_LIBS) \
	$(NULL)


noinst_LTLIBRARIES = \
	pam_gdm_testlist.la \
	$(NULL)
endif

EXTRA_DIST =                                                            \
        gdm-pam-extensions.pc                                           \
        $(NULL)
