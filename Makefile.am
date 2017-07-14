NULL =

SUBDIRS = 			\
	data			\
	common 			\
	pam-extensions		\
	daemon 			\
	libgdm 			\
	utils 			\
	pam_gdm			\
	po 			\
	tests 			\
	$(NULL)

if XDMCP_SUPPORT
SUBDIRS += chooser
endif

if ENABLE_DOCUMENTATION
SUBDIRS += docs
endif

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

EXTRA_DIST = \
	MAINTAINERS		\
	ChangeLog		\
	README			\
	$(NULL)

DISTCLEANFILES = \
	$(NULL)

AM_DISTCHECK_CONFIGURE_FLAGS = --enable-split-authentication --enable-introspection --without-systemdsystemunitdir
