NULL =

SUBDIRS = 			\
	data			\
	common 			\
	daemon 			\
	gui 			\
	utils 			\
	po 			\
	tests 			\
	$(NULL)
if ENABLE_DOCUMENTATION
SUBDIRS += docs
endif

EXTRA_DIST = \
	MAINTAINERS		\
	ChangeLog		\
	README			\
	$(NULL)

DISTCLEANFILES = \
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS = --enable-split-authentication --enable-introspection
