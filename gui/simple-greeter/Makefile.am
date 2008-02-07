NULL =

SUBDIRS = 				\
	libnotificationarea		\
	$(NULL)

INCLUDES = \
	-I$(top_srcdir)/common				\
	-I$(top_srcdir)/gui/simple-greeter/libnotificationarea	\
	-DDMCONFDIR=\""$(dmconfdir)"\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"                  \
	-DDATADIR=\""$(datadir)"\"		 	\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DLIBLOCALEDIR=\""$(prefix)/lib/locale"\"	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGLADEDIR=\""$(pkgdatadir)"\" 	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	-DAT_SPI_REGISTRYD_DIR="\"$(AT_SPI_REGISTRYD_DIR)\""	\
	$(GTK_CFLAGS)					\
	$(SIMPLE_GREETER_CFLAGS)			\
	$(POLKIT_GNOME_CFLAGS)				\
	$(NULL)

@INTLTOOL_SCHEMAS_RULE@

schemasdir   = @GCONF_SCHEMA_FILE_DIR@
schemas_in_files = gdm-simple-greeter.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

noinst_LTLIBRARIES = 			\
	libgdmuser.la			\
	$(null)

libgdmuser_la_SOURCES =			\
	gdm-user.c			\
	gdm-user.h			\
	gdm-user-private.h		\
	gdm-user-manager.c		\
	gdm-user-manager.h		\
	$(NULL)

libgdmuser_la_CPPFLAGS = 		\
	$(DISABLE_DEPRECATED_CFLAGS)	\
	$(NULL)

libgdmuser_la_CFLAGS =			\
	$(SIMPLE_GREETER_CFLAGS)	\
	$(NULL)

libgdmuser_la_LIBADD =			\
	$(NULL)

libgdmuser_la_LDFLAGS = 	\
	-export-dynamic 	\
	$(NULL)

noinst_PROGRAMS = 			\
	test-greeter-login-window	\
	test-remote-login-window	\
	test-greeter-panel		\
	test-language-chooser		\
	test-session-chooser		\
	test-user-chooser		\
	test-user-manager		\
	$(NULL)

test_greeter_login_window_SOURCES = 	\
	test-greeter-login-window.c	\
	gdm-greeter-login-window.h	\
	gdm-greeter-login-window.c	\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-sessions.h			\
	gdm-sessions.c			\
	gdm-session-chooser-widget.h	\
	gdm-session-chooser-widget.c	\
	gdm-user-chooser-widget.h	\
	gdm-user-chooser-widget.c	\
	gdm-user-chooser-dialog.h	\
	gdm-user-chooser-dialog.c	\
	$(NULL)

test_greeter_login_window_LDADD =	\
	$(top_builddir)/common/libgdmcommon.la	\
	libgdmuser.la			\
	$(COMMON_LIBS)			\
	$(SIMPLE_GREETER_LIBS)		\
	$(POLKIT_GNOME_LIBS)		\
	$(RBAC_LIBS)			\
	$(NULL)

test_greeter_panel_SOURCES = 	\
	test-greeter-panel.c	\
	gdm-greeter-panel.h	\
	gdm-greeter-panel.c	\
	gdm-clock-widget.h      \
	gdm-clock-widget.c      \
	gdm-option-widget.h     \
	gdm-option-widget.c     \
	gdm-recent-option-widget.h	\
	gdm-recent-option-widget.c	\
	gdm-languages.h			\
	gdm-languages.c			\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-language-chooser-widget.h	\
	gdm-language-chooser-widget.c	\
	locarchive.h			\
	gdm-language-chooser-dialog.h	\
	gdm-language-chooser-dialog.c	\
	gdm-language-option-widget.h	\
	gdm-language-option-widget.c	\
	gdm-sessions.h			\
	gdm-sessions.c			\
	gdm-session-option-widget.h	\
	gdm-session-option-widget.c	\
	$(NULL)

test_greeter_panel_LDADD =	\
	$(top_builddir)/gui/simple-greeter/libnotificationarea/libnotificationarea.la	\
	$(GTK_LIBS)		\
	$(GCONF_LIBS)		\
	$(NULL)

test_remote_login_window_SOURCES = 	\
	test-remote-login-window.c	\
	gdm-remote-login-window.h	\
	gdm-remote-login-window.c	\
	$(NULL)

test_remote_login_window_LDADD =	\
	$(GTK_LIBS)			\
	$(NULL)

test_language_chooser_SOURCES = 	\
	test-language-chooser.c		\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-languages.h			\
	gdm-languages.c			\
	gdm-language-chooser-widget.h	\
	gdm-language-chooser-widget.c	\
	locarchive.h			\
	gdm-language-chooser-dialog.h	\
	gdm-language-chooser-dialog.c	\
	$(NULL)

test_language_chooser_LDADD =	\
	$(GTK_LIBS)		\
	$(NULL)

test_session_chooser_SOURCES = 	\
	test-session-chooser.c		\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-sessions.h			\
	gdm-sessions.c			\
	gdm-session-chooser-widget.h	\
	gdm-session-chooser-widget.c	\
	gdm-session-chooser-dialog.h	\
	gdm-session-chooser-dialog.c	\
	$(NULL)

test_session_chooser_LDADD =		\
	$(GTK_LIBS)			\
	$(NULL)

test_user_chooser_SOURCES = 		\
	test-user-chooser.c		\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-user-chooser-widget.h	\
	gdm-user-chooser-widget.c	\
	gdm-user-chooser-dialog.h	\
	gdm-user-chooser-dialog.c	\
	$(NULL)

test_user_chooser_LDADD =	\
	libgdmuser.la		\
	$(COMMON_LIBS)		\
	$(SIMPLE_GREETER_LIBS)	\
	$(POLKIT_GNOME_LIBS)	\
	$(NULL)

test_user_manager_SOURCES = 	\
	test-user-manager.c	\
	$(NULL)

test_user_manager_LDADD =	\
	libgdmuser.la		\
	$(COMMON_LIBS)		\
	$(SIMPLE_GREETER_LIBS)	\
	$(POLKIT_GNOME_LIBS)	\
	$(NULL)

libexec_PROGRAMS =			\
	gdm-simple-greeter

gdm_simple_greeter_SOURCES =  		\
	greeter-main.c 			\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-greeter-client.h		\
	gdm-greeter-client.c		\
	gdm-greeter-session.h		\
	gdm-greeter-session.c		\
	gdm-session-manager.h		\
	gdm-session-manager.c		\
	gdm-session-client.h		\
	gdm-session-client.c		\
	gdm-greeter-login-window.c	\
	gdm-greeter-login-window.h	\
	gdm-remote-login-window.c	\
	gdm-remote-login-window.h	\
	gdm-greeter-panel.h		\
	gdm-greeter-panel.c		\
	gdm-clock-widget.h		\
	gdm-clock-widget.c		\
	gdm-option-widget.h		\
	gdm-option-widget.c		\
	gdm-recent-option-widget.h	\
	gdm-recent-option-widget.c	\
	gdm-languages.h			\
	gdm-languages.c			\
	gdm-language-chooser-widget.h	\
	gdm-language-chooser-widget.c	\
	locarchive.h			\
	gdm-language-chooser-dialog.h	\
	gdm-language-chooser-dialog.c	\
	gdm-language-option-widget.h	\
	gdm-language-option-widget.c	\
	gdm-sessions.h			\
	gdm-sessions.c			\
	gdm-session-option-widget.h	\
	gdm-session-option-widget.c	\
	gdm-session-chooser-widget.h	\
	gdm-session-chooser-widget.c	\
	gdm-user-chooser-widget.h	\
	gdm-user-chooser-widget.c	\
	$(NULL)

gdm_simple_greeter_LDADD = 		\
	$(top_builddir)/common/libgdmcommon.la	\
	libgdmuser.la			\
	$(top_builddir)/gui/simple-greeter/libnotificationarea/libnotificationarea.la	\
	$(COMMON_LIBS)			\
	$(EXTRA_GREETER_LIBS)   	\
	$(SIMPLE_GREETER_LIBS)		\
	$(POLKIT_GNOME_LIBS)		\
	$(RBAC_LIBS)			\
	$(NULL)

gladedir = $(pkgdatadir)
glade_DATA = 				\
	gdm-greeter-login-window.glade	\
	$(NULL)

EXTRA_DIST = 			\
	$(schemas_in_files)	\
	$(glade_DATA)		\
	$(NULL)

DISTCLEANFILES =                        \
        $(schemas_DATA)                 \
        $(NULL)

MAINTAINERCLEANFILES =                  \
        *~                              \
        Makefile.in

if GCONF_SCHEMAS_INSTALL
# don't do this if we are building in eg. rpm
install-data-local: $(schemas_DATA)
	if test -z "$(DESTDIR)" ; then \
		for p in $(schemas_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/gui/simple-greeter/$$p ; \
		done \
	fi
else
install-data-local:
endif
