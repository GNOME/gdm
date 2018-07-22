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
	README.md		\
	meson.build		\
	utils/meson.build		\
	chooser/meson.build		\
	libgdm/meson.build		\
	tests/meson.build		\
	data/applications/meson.build		\
	data/meson.build		\
	data/dconf/meson.build		\
	data/autostart/meson.build		\
	po/meson.build		\
	daemon/meson.build		\
	docs/meson.build		\
	common/meson.build		\
	pam-extensions/meson.build		\
	pam_gdm/meson.build		\
	$(NULL)

DISTCLEANFILES = \
	$(NULL)

AM_DISTCHECK_CONFIGURE_FLAGS = --enable-split-authentication --enable-introspection --without-systemdsystemunitdir --without-udevdir
