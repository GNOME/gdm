END_OF_LIST =

AM_CPPFLAGS = \
        -I$(srcdir)                                                     \
        -I$(builddir)                                                   \
        -I$(top_srcdir)                                                 \
        -I$(top_builddir)                                               \
        -DG_LOG_DOMAIN=\"Gdm\"                                          \
        -DDMCONFDIR=\""$(dmconfdir)"\"                                  \
        -DDATADIR=\""$(datadir)"\"                                      \
        $(END_OF_LIST)

pam_gdm_la_SOURCES = \
	pam_gdm.c \
	$(END_OF_LIST)

pam_gdm_la_CFLAGS = \
	$(KEYUTILS_CFLAGS) \
	$(PAM_CFLAGS) \
	$(END_OF_LIST)

pam_gdm_la_LDFLAGS = \
	-module \
	-export-dynamic \
	-avoid-version \
	-shared \
	$(END_OF_LIST)

pam_gdm_la_LIBADD = \
	$(KEYUTILS_LIBS) \
	$(PAM_LIBS) \
	$(END_OF_LIST)


pam_gdm_LTLIBRARIES = \
	pam_gdm.la \
	$(END_OF_LIST)

pam_gdmdir = $(libdir)/security
