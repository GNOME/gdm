NULL =

INCLUDES = \
	-I.						\
	-I..						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	$(UTILS_CFLAGS)					\
	$(GTK_CFLAGS)					\
	$(COMMON_CFLAGS)				\
	$(NULL)

edit = sed \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@GDM_PID_FILE[@]|$(GDM_PID_FILE)|g'

gdm-stop: gdm-stop.in Makefile
	$(edit) $< >$@
gdm-restart: gdm-restart.in Makefile
	$(edit) $< >$@
gdm-safe-restart: gdm-safe-restart.in Makefile
	$(edit) $< >$@

bin_PROGRAMS = \
	gdmflexiserver		\
	$(NULL)

sbin_SCRIPTS = 			\
	gdm-stop		\
	gdm-restart		\
	gdm-safe-restart	\
	$(NULL)

gdmflexiserver_SOURCES =	\
	gdmflexiserver.c	\
	$(NULL)

gdmflexiserver_LDADD =		\
	$(GTK_LIBS)		\
	$(COMMON_LIBS)		\
	$(NULL)

EXTRA_DIST = 			\
	gdm-stop.in		\
	gdm-restart.in		\
	gdm-safe-restart.in	\
	$(NULL)

CLEANFILES = 			\
	$(NULL)

DISTCLEANFILES = 		\
	gdm-stop		\
	gdm-restart		\
	gdm-safe-restart	\
	$(NULL)

