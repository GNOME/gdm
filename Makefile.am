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
	meson_options.txt		\
	chooser/meson.build		\
	common/meson.build		\
	daemon/meson.build		\
	data/applications/meson.build		\
	data/autostart/meson.build		\
	data/dconf/meson.build		\
	data/meson.build		\
	docs/meson.build		\
	libgdm/meson.build		\
	pam-extensions/meson.build		\
	pam_gdm/meson.build		\
	po/meson.build		\
	tests/meson.build		\
	utils/meson.build		\
	$(NULL)

DISTCLEANFILES = \
	$(NULL)

AM_DISTCHECK_CONFIGURE_FLAGS = --enable-split-authentication --enable-introspection --without-systemdsystemunitdir --without-udevdir
