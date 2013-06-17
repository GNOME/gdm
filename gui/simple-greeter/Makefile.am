NULL =
SUBDIRS = 				\
	libgdmsimplegreeter		\
	extensions				\
	$(NULL)

AM_CPPFLAGS = \
	-I$(top_srcdir)/common				\
	-I$(top_builddir)/common			\
	-I$(top_srcdir)/gui/libgdm			\
	-I$(top_builddir)/gui/libgdm			\
	-I$(top_srcdir)/gui/simple-greeter/libgdmsimplegreeter	\
	-DDMCONFDIR=\""$(dmconfdir)"\"			\
	-DGDMCONFDIR=\"$(gdmconfdir)\"                  \
	-DDATADIR=\""$(datadir)"\"		 	\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DLIBLOCALEDIR=\""$(prefix)/lib/locale"\"	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DUIDIR=\""$(pkgdatadir)"\"			\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DSBINDIR=\""$(sbindir)"\"		 	\
	-DGDM_CACHE_DIR=\""$(localstatedir)/cache/gdm"\"	\
	-DAT_SPI_REGISTRYD_DIR="\"$(AT_SPI_REGISTRYD_DIR)\""	\
	$(UPOWER_CFLAGS)				\
	-DGDM_SIMPLE_GREETER_PLUGINS_DIR="\"$(GDM_SIMPLE_GREETER_PLUGINS_DIR)\""\
	$(DISABLE_DEPRECATED_CFLAGS)			\
	$(GTK_CFLAGS)					\
	$(SIMPLE_GREETER_CFLAGS)			\
	$(NULL)

noinst_PROGRAMS = 			\
	test-filesystem-type		\
	test-greeter-login-window	\
	test-greeter-panel		\
	test-sessions			\
	test-remote-login-window	\
	test-user-chooser		\
	test-user-manager		\
	$(NULL)

gsm-client-glue.c gsm-client-glue.h : org.gnome.SessionManager.ClientPrivate.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace Gsm				\
		--interface-prefix=org.gnome.SessionManager		\
		--generate-c-code=gsm-client-glue			\
		$(srcdir)/org.gnome.SessionManager.ClientPrivate.xml

gsm-manager-glue.c gsm-manager-glue.h : org.gnome.SessionManager.xml Makefile.am
	$(AM_V_GEN)gdbus-codegen 					\
		--c-namespace Gsm				\
		--interface-prefix=org.gnome.SessionManager		\
		--generate-c-code=gsm-manager-glue			\
		--annotate "org.gnome.SessionManager"			\
		           "org.gtk.GDBus.C.Name" Manager		\
		$(srcdir)/org.gnome.SessionManager.xml

test_greeter_login_window_SOURCES = 	\
	test-greeter-login-window.c	\
	gdm-timer.h			\
	gdm-timer.c			\
	gdm-greeter-login-window.h	\
	gdm-greeter-login-window.c	\
	gdm-scrollable-widget.h		\
	gdm-scrollable-widget.c		\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-sessions.h			\
	gdm-sessions.c			\
	gdm-cell-renderer-timer.h	\
	gdm-cell-renderer-timer.c	\
	gdm-option-widget.h		\
	gdm-option-widget.c		\
	gdm-session-option-widget.h	\
	gdm-session-option-widget.c	\
	gdm-user-chooser-widget.h	\
	gdm-user-chooser-widget.c	\
	gdm-user-chooser-dialog.h	\
	gdm-user-chooser-dialog.c	\
	gdm-extension-list.h		\
	gdm-extension-list.c		\
	$(NULL)

test_greeter_login_window_LDADD =	\
	$(top_builddir)/common/libgdmcommon.la	\
	$(top_builddir)/gui/libgdm/libgdm.la	\
	$(top_builddir)/gui/simple-greeter/libgdmsimplegreeter/libgdmsimplegreeter.la	\
	$(top_builddir)/gui/simple-greeter/extensions/unified/libunified.la	\
	$(COMMON_LIBS)			\
	$(SIMPLE_GREETER_LIBS)		\
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
	gdm-cell-renderer-timer.h	\
	gdm-cell-renderer-timer.c	\
	gdm-timer.h			\
	gdm-timer.c			\
	gdm-scrollable-widget.h		\
	gdm-scrollable-widget.c		\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-sessions.h			\
	gdm-sessions.c			\
	gdm-session-option-widget.h	\
	gdm-session-option-widget.c	\
	$(NULL)

test_greeter_panel_LDADD =	\
	$(top_builddir)/common/libgdmcommon.la	\
	$(top_builddir)/gui/simple-greeter/libgdmsimplegreeter/libgdmsimplegreeter.la	\
	$(SIMPLE_GREETER_LIBS)		\
	$(GTK_LIBS)			\
	$(UPOWER_LIBS)		\
	$(NULL)

test_remote_login_window_SOURCES = 	\
	test-remote-login-window.c	\
	gdm-remote-login-window.h	\
	gdm-remote-login-window.c	\
	$(NULL)

test_remote_login_window_LDADD =	\
	$(GTK_LIBS)			\
	$(top_builddir)/common/libgdmcommon.la	\
	$(NULL)

test_filesystem_type_SOURCES = 	\
	test-filesystem-type.c	\
	$(NULL)

test_filesystem_type_LDADD =	\
	$(COMMON_LIBS)			\
	$(NULL)

test_sessions_SOURCES = 		\
	test-sessions.c		\
	gdm-sessions.h			\
	gdm-sessions.c			\
	$(NULL)

test_sessions_LDADD =		\
	$(GTK_LIBS)		\
	$(NULL)

test_user_chooser_SOURCES = 		\
	test-user-chooser.c		\
	gdm-timer.h			\
	gdm-timer.c			\
	gdm-cell-renderer-timer.h	\
	gdm-cell-renderer-timer.c	\
	gdm-scrollable-widget.h		\
	gdm-scrollable-widget.c		\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-user-chooser-widget.h	\
	gdm-user-chooser-widget.c	\
	gdm-user-chooser-dialog.h	\
	gdm-user-chooser-dialog.c	\
	$(NULL)

test_user_chooser_LDADD =	\
	$(top_builddir)/common/libgdmcommon.la	\
	$(COMMON_LIBS)		\
	$(SIMPLE_GREETER_LIBS)	\
	$(NULL)

test_user_manager_SOURCES = 	\
	test-user-manager.c	\
	$(NULL)

test_user_manager_LDADD =	\
	$(top_builddir)/common/libgdmcommon.la	\
	$(COMMON_LIBS)		\
	$(SIMPLE_GREETER_LIBS)	\
	$(NULL)

libexec_PROGRAMS =			\
	gdm-simple-greeter

gdm_simple_greeter_SOURCES =  		\
	greeter-main.c 			\
	gdm-timer.h			\
	gdm-timer.c			\
	gdm-cell-renderer-timer.h	\
	gdm-cell-renderer-timer.c	\
	gdm-scrollable-widget.h		\
	gdm-scrollable-widget.c		\
	gdm-chooser-widget.h		\
	gdm-chooser-widget.c		\
	gdm-greeter-session.h		\
	gdm-greeter-session.c		\
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
	gdm-sessions.h			\
	gdm-sessions.c			\
	gdm-session-option-widget.h	\
	gdm-session-option-widget.c	\
	gdm-user-chooser-widget.h	\
	gdm-user-chooser-widget.c	\
	gdm-extension-list.h		\
	gdm-extension-list.c		\
	$(NULL)

gdm_simple_greeter_LDADD = 		\
	$(top_builddir)/common/libgdmcommon.la	\
	$(top_builddir)/gui/libgdm/libgdm.la	\
	$(top_builddir)/gui/simple-greeter/libgdmsimplegreeter/libgdmsimplegreeter.la	\
	$(top_builddir)/gui/simple-greeter/extensions/unified/libunified.la	\
	$(COMMON_LIBS)			\
	$(EXTRA_GREETER_LIBS)   	\
	$(SIMPLE_GREETER_LIBS)		\
	$(RBAC_LIBS)			\
	$(UPOWER_LIBS)		\
	$(NULL)

nodist_gdm_simple_greeter_SOURCES =     \
	gsm-manager-glue.c		\
	gsm-manager-glue.h		\
	gsm-client-glue.c		\
	gsm-client-glue.h

CLEANFILES = \
	gsm-manager-glue.c		\
	gsm-manager-glue.h		\
	gsm-client-glue.c		\
	gsm-client-glue.h

BUILT_SOURCES = gsm-client-glue.h gsm-manager-glue.h

uidir = $(pkgdatadir)
ui_DATA = 					\
	gdm-greeter-login-window.ui		\
	$(NULL)

EXTRA_DIST = 						\
	org.gnome.SessionManager.ClientPrivate.xml      \
	org.gnome.SessionManager.xml                    \
	$(ui_DATA)					\
	$(NULL)

MAINTAINERCLEANFILES =                  \
        *~                              \
        Makefile.in
