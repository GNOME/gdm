NULL =

SUBDIRS = 			\
	data			\
	common 			\
	daemon 			\
	libgdm 			\
	gui 			\
	utils 			\
	po 			\
	tests 			\
	$(NULL)
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

DISTCHECK_CONFIGURE_FLAGS = --enable-split-authentication --enable-introspection
