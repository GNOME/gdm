NULL =

SUBDIRS = 			\
	po 			\
	config 			\
	pixmaps 		\
	common 			\
	daemon 			\
	gui 			\
	utils 			\
	docs			\
	$(NULL)

# add these when help gets added back
# docs omf-install

EXTRA_DIST = \
	MAINTAINERS		\
	ChangeLog		\
	README			\
	README.install		\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	gnome-doc-utils.make	\
	xmldocs.make		\
	omf.make

DISTCLEANFILES = \
	gnome-doc-utils.make	\
	intltool-extract	\
	intltool-merge		\
	intltool-update

DISTCHECK_CONFIGURE_FLAGS = --disable-scrollkeeper

distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper'
