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
endif

EXTRA_DIST =                                                            \
        gdm-pam-extensions.pc                                           \
        $(NULL)
