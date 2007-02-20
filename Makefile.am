SUBDIRS = \
	po \
	config \
	pixmaps \
	vicious-extensions \
	daemon \
	gui \
	utils \
	docs

# add these when help gets added back
# docs omf-install

EXTRA_DIST = \
	MAINTAINERS		\
	ChangeLog		\
	README			\
	README.install		\
	gdm.spec		\
	gdm.spec.in		\
	gdmsetup-pam		\
	gdmsetup-security.in	\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	gdm-stop.in		\
	gdm-restart.in		\
	gdm-safe-restart.in	\
	gnome-doc-utils.make	\
	xmldocs.make		\
	omf.make

sbin_SCRIPTS = gdm-stop gdm-restart gdm-safe-restart

if CONSOLE_HELPER
cappsdir = $(PAM_PREFIX)/security/console.apps
capps_DATA = gdmsetup

CLEANFILES = gdmsetup-security
gdmsetup: $(srcdir)/gdmsetup-security.in
	sed -e 's,[@]sbindir[@],$(sbindir),g' <$(srcdir)/gdmsetup-security.in >gdmsetup

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(PAM_PREFIX)/pam.d
	$(INSTALL_DATA) $(srcdir)/gdmsetup-pam $(DESTDIR)$(PAM_PREFIX)/pam.d/gdmsetup
else
install-data-local:
endif

DISTCLEANFILES = \
	gdmsetup		\
	gnome-doc-utils.make	\
	intltool-extract	\
	intltool-merge		\
	intltool-update

DISTCHECK_CONFIGURE_FLAGS = --disable-scrollkeeper

distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper'
