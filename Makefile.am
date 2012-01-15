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

# add these when help gets added back
# omf-install

EXTRA_DIST = \
	MAINTAINERS		\
	ChangeLog		\
	README			\
	gnome-doc-utils.make	\
	xmldocs.make		\
	omf.make		\
	$(NULL)

DISTCLEANFILES = \
	gnome-doc-utils.make	\
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS = --disable-scrollkeeper --enable-split-authentication --enable-introspection --enable-documentation

distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper'
