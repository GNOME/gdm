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
	-DLIBLOCALEDIR=\""$(libdir)/locale"\"		\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGLADEDIR=\""$(pkgdatadir)"\" 	\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	-DAT_SPI_REGISTRYD_DIR="\"$(AT_SPI_REGISTRYD_DIR)\""	\
	$(GTK_CFLAGS)					\
	$(SIMPLE_GREETER_CFLAGS)			\
	$(NULL)

noinst_PROGRAMS = 			\
	test-greeter-background		\
	test-greeter-login-window	\
	test-greeter-panel		\
	test-language-chooser		\
	test-session-chooser		\
	test-user-chooser		\
	test-user-manager		\
	$(NULL)

test_greeter_background_SOURCES = 	\
	test-greeter-background.c	\
	gdm-greeter-background.h	\
	gdm-greeter-background.c	\
	$(NULL)

test_greeter_background_LDADD =	\
	$(SIMPLE_GREETER_LIBS)	\
	$(NULL)

test_greeter_login_window_SOURCES = 	\
	test-greeter-login-window.c	\
	gdm-greeter-login-window.h	\
	gdm-greeter-login-window.c	\
	gdm-user-chooser-widget.h	\
	gdm-user-chooser-widget.c	\
	gdm-user-chooser-dialog.h	\
	gdm-user-chooser-dialog.c	\
	gdm-user-manager.h		\
	gdm-user-manager.c		\
	gdm-user-private.h		\
	gdm-user.h			\
	gdm-user.c			\
	$(NULL)

test_greeter_login_window_LDADD =	\
	$(SIMPLE_GREETER_LIBS)		\
	$(NULL)

test_greeter_panel_SOURCES = 	\
	test-greeter-panel.c	\
	gdm-greeter-panel.h	\
	gdm-greeter-panel.c	\
	$(NULL)

test_greeter_panel_LDADD =	\
	$(GTK_LIBS)		\
	$(top_builddir)/gui/simple-greeter/libnotificationarea/libnotificationarea.la	\
	$(NULL)

test_language_chooser_SOURCES = 	\
	test-language-chooser.c		\
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
	gdm-user-chooser-widget.h	\
	gdm-user-chooser-widget.c	\
	gdm-user-chooser-dialog.h	\
	gdm-user-chooser-dialog.c	\
	gdm-user-manager.h		\
	gdm-user-manager.c		\
	gdm-user-private.h		\
	gdm-user.h			\
	gdm-user.c			\
	$(NULL)

test_user_chooser_LDADD =	\
	$(SIMPLE_GREETER_LIBS)	\
	$(NULL)

test_user_manager_SOURCES = 	\
	test-user-manager.c	\
	gdm-user-manager.h	\
	gdm-user-manager.c	\
	gdm-user-private.h	\
	gdm-user.h		\
	gdm-user.c		\
	$(NULL)

test_user_manager_LDADD =	\
	$(SIMPLE_GREETER_LIBS)	\
	$(NULL)

libexec_PROGRAMS =			\
	gdm-simple-greeter

gdm_simple_greeter_SOURCES =  		\
	greeter-main.c 			\
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
	gdm-greeter-panel.h		\
	gdm-greeter-panel.c		\
	gdm-greeter-background.h	\
	gdm-greeter-background.c	\
	gdm-user-chooser-widget.h	\
	gdm-user-chooser-widget.c	\
	gdm-user-manager.h		\
	gdm-user-manager.c		\
	gdm-user-private.h		\
	gdm-user.h			\
	gdm-user.c			\
	$(NULL)

gdm_simple_greeter_LDADD = 		\
	$(EXTRA_GREETER_LIBS)   	\
	$(top_builddir)/common/libgdmcommon.la	\
	$(top_builddir)/gui/simple-greeter/libnotificationarea/libnotificationarea.la	\
	$(SIMPLE_GREETER_LIBS)		\
	$(NULL)

gladedir = $(pkgdatadir)
glade_DATA = 				\
	gdm-greeter-login-window.glade	\
	$(NULL)

EXTRA_DIST = 			\
	$(glade_DATA)		\
	$(NULL)
