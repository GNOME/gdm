/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef GDM_H
#define GDM_H

#include <glib.h>
#include <X11/Xlib.h>
#include <X11/Xmd.h>
#include <X11/Xauth.h>
#include <netinet/in.h>
#include <time.h>

#define STX 0x2			/* Start of txt */
#define BEL 0x7			/* Bell, used to interrupt login for
				 * say timed login or something similar */

#define TYPE_LOCAL 1		/* Local X server */
#define TYPE_XDMCP 2		/* Remote display */
#define TYPE_FLEXI 3		/* Local Flexi X server */
#define TYPE_FLEXI_XNEST 4	/* Local Flexi Xnest server */

#define SERVER_IS_LOCAL(d) (d->type == TYPE_LOCAL || \
			    d->type == TYPE_FLEXI || \
			    d->type == TYPE_FLEXI_XNEST)
#define SERVER_IS_FLEXI(d) (d->type == TYPE_FLEXI || \
			    d->type == TYPE_FLEXI_XNEST)

/* These are the servstat values, also used as server
 * process exit codes */
#define SERVER_TIMEOUT 2	/* Server didn't start */
#define SERVER_DEAD 250		/* Server stopped */
#define SERVER_STARTED 251	/* Server started but not ready for connections yet */
#define SERVER_RUNNING 252	/* Server running and ready for connections */
#define SERVER_ABORT 253	/* Server failed badly. Suspending display. */

/* DO NOTE USE 1, that's used as error if x connection fails usually */
/* Note that there is no reasons why these were a power of two, and note
 * that they have to fit in 256 */
/* These are the exit codes */
#define DISPLAY_REMANAGE 2	/* Restart display */
#define DISPLAY_ABORT 4		/* Houston, we have a problem */
#define DISPLAY_REBOOT 8	/* Rebewt */
#define DISPLAY_HALT 16		/* Halt */
#define DISPLAY_SUSPEND 17	/* Suspend */
#define DISPLAY_CHOSEN 20	/* successful chooser session,
				   restart display */
#define DISPLAY_XFAILED 64	/* X failed */
#define DISPLAY_RESTARTGDM 128	/* Restart GDM */

enum {
	DISPLAY_UNBORN /* Not yet started */,
	DISPLAY_ALIVE /* Yay! we're alive (non-xdmcp) */,
	XDMCP_PENDING /* Pending XDMCP display */,
	XDMCP_MANAGED /* Managed XDMCP display */,
	DISPLAY_DEAD /* Left for dead */
};

/* Opcodes for the highly sophisticated protocol used for
 * daemon<->greeter communications */

/* This will change if there are incompatible
 * protocol changes */
#define GDM_GREETER_PROTOCOL_VERSION "1"

#define GDM_MSG        'D'
#define GDM_NOECHO     'U'
#define GDM_PROMPT     'N'
#define GDM_SESS       'G'
#define GDM_LANG       '&'
#define GDM_SSESS      'C'
#define GDM_SLANG      'R'
#define GDM_RESET      'A'
#define GDM_QUIT       'P'
/* Well these aren't as nice as above, oh well */
#define GDM_GNOMESESS  '?'
#define GDM_SGNOMESESS '*'
#define GDM_STARTTIMER 's'
#define GDM_STOPTIMER  'S'
#define GDM_LOGIN      'L' /* this is the login prompt, much like PROMPT but
			      different */
#define GDM_SETLOGIN   'l' /* this just sets the login to be this, just for
			      the greeters knowledge */
#define GDM_DISABLE    '-' /* disable the login screen */
#define GDM_ENABLE     '+' /* enable the login screen */
#define GDM_RESETOK    'r' /* reset but don't shake */
#define GDM_NEEDPIC    '#' /* need a user picture?, sent after greeter
			    *  is started */
#define GDM_READPIC    '%' /* Send a user picture in a temp file */
#define GDM_ERRBOX     'e' /* Puts string in the error box */
#define GDM_ERRDLG     'E' /* Puts string up in an error dialog */

/* Different login interruptions */
#define GDM_INTERRUPT_TIMED_LOGIN 'T'
#define GDM_INTERRUPT_CONFIGURE   'C'

/* The dreaded miscellaneous category */
#define FIELD_SIZE 256
#define PIPE_SIZE 4096

/* Configuration constants */
#define GDM_KEY_CHOOSER "daemon/Chooser=" EXPANDED_BINDIR "/gdmchooser --disable-sound --disable-crash-dialog"
/* This defaults to true for backward compatibility,
 * it will not actually do automatic login since the AutomaticLogin defaults
 * to nothing */
#define GDM_KEY_AUTOMATICLOGIN_ENABLE "daemon/AutomaticLoginEnable=true"
#define GDM_KEY_AUTOMATICLOGIN "daemon/AutomaticLogin="
#define GDM_KEY_ALWAYSRESTARTSERVER "daemon/AlwaysRestartServer=false"
#define GDM_KEY_GREETER "daemon/Greeter=" EXPANDED_BINDIR "/gdmlogin --disable-sound --disable-crash-dialog"
#define GDM_KEY_GROUP "daemon/Group=gdm"
#define GDM_KEY_HALT "daemon/HaltCommand=/sbin/shutdown -h now"
#define GDM_KEY_INITDIR "daemon/DisplayInitDir=" EXPANDED_SYSCONFDIR "/gdm/Init"
#define GDM_KEY_KILLIC "daemon/KillInitClients=true"
#define GDM_KEY_LOGDIR "daemon/LogDir=" EXPANDED_AUTHDIR
#define GDM_KEY_PATH "daemon/DefaultPath=/bin:/usr/bin:/usr/bin/X11:/usr/local/bin:" EXPANDED_BINDIR
#define GDM_KEY_PIDFILE "daemon/PidFile=/var/run/gdm.pid"
#define GDM_KEY_POSTSESS "daemon/PostSessionScriptDir=" EXPANDED_SYSCONFDIR "/gdm/PostSession/"
#define GDM_KEY_PRESESS "daemon/PreSessionScriptDir=" EXPANDED_SYSCONFDIR "/gdm/PreSession/"
#define GDM_KEY_FAILSAFE_XSERVER "daemon/FailsafeXServer="
#define GDM_KEY_XKEEPSCRASHING "daemon/XKeepsCrashing=" EXPANDED_SYSCONFDIR "/gdm/XKeepsCrashing"
#define GDM_KEY_REBOOT "daemon/RebootCommand=/sbin/shutdown -r now"
#define GDM_KEY_ROOTPATH "daemon/RootPath=/sbin:/usr/sbin:/bin:/usr/bin:/usr/bin/X11:/usr/local/bin:" EXPANDED_BINDIR
#define GDM_KEY_GNOMEDEFAULTSESSION "daemon/GnomeDefaultSession=" EXPANDED_DATADIR "/gnome/default.session"
#define GDM_KEY_SERVAUTH "daemon/ServAuthDir=" EXPANDED_AUTHDIR
#define GDM_KEY_SESSDIR "daemon/SessionDir=" EXPANDED_SYSCONFDIR "/gdm/Sessions/"
#define GDM_KEY_SUSPEND "daemon/SuspendCommand="

#define GDM_KEY_UAUTHDIR "daemon/UserAuthDir="
#define GDM_KEY_UAUTHFB "daemon/UserAuthFBDir=/tmp"
#define GDM_KEY_UAUTHFILE "daemon/UserAuthFile=.Xauthority"
#define GDM_KEY_USER "daemon/User=gdm"

/* This defaults to true for backward compatibility,
 * it will not actually do timed login since the TimedLogin defaults
 * to nothing */
#define GDM_KEY_TIMED_LOGIN_ENABLE "daemon/TimedLoginEnable=true"
#define GDM_KEY_TIMED_LOGIN "daemon/TimedLogin="
#define GDM_KEY_TIMED_LOGIN_DELAY "daemon/TimedLoginDelay=30"

#define GDM_KEY_STANDARD_XSERVER "daemon/StandardXServer=/usr/bin/X11/X"
#define GDM_KEY_FLEXIBLE_XSERVERS "daemon/FlexibleXServers=5"
#define GDM_KEY_XNEST "daemon/Xnest=/usr/bin/X11/Xnest -name Xnest"

/* Per server definitions */
#define GDM_KEY_SERVER_NAME "name=Standard server"
#define GDM_KEY_SERVER_COMMAND "command=/usr/bin/X11/X"
/* runnable as flexi server */
#define GDM_KEY_SERVER_FLEXIBLE "flexible=true"
/* choosable from the login screen */
#define GDM_KEY_SERVER_CHOOSABLE "choosable=true"

#define GDM_KEY_ALLOWROOT "security/AllowRoot=true"
#define GDM_KEY_ALLOWREMOTEROOT "security/AllowRemoteRoot=true"
#define GDM_KEY_ALLOWREMOTEAUTOLOGIN "security/AllowRemoteAutoLogin=false"
#define GDM_KEY_MAXFILE "security/UserMaxFile=65536"
#define GDM_KEY_SESSIONMAXFILE "security/SessionMaxFile=524288"
#define GDM_KEY_RELAXPERM "security/RelaxPermissions=0"
#define GDM_KEY_RETRYDELAY "security/RetryDelay=3"

#define GDM_KEY_XDMCP "xdmcp/Enable=false"
#define GDM_KEY_MAXPEND "xdmcp/MaxPending=4"
#define GDM_KEY_MAXSESS "xdmcp/MaxSessions=16"
#define GDM_KEY_MAXWAIT "xdmcp/MaxWait=30"
#define GDM_KEY_DISPERHOST "xdmcp/DisplaysPerHost=1"
#define GDM_KEY_UDPPORT "xdmcp/Port=177"
#define GDM_KEY_INDIRECT "xdmcp/HonorIndirect=true"
#define GDM_KEY_MAXINDIR "xdmcp/MaxPendingIndirect=4"
#define GDM_KEY_MAXINDWAIT "xdmcp/MaxWaitIndirect=30"
#define GDM_KEY_PINGINTERVAL "xdmcp/PingInterval=5"
#define GDM_KEY_WILLING "xdmcp/Willing=" EXPANDED_SYSCONFDIR "/gdm/Xwilling"

#define GDM_KEY_GTKRC "gui/GtkRC=" EXPANDED_DATADIR "/themes/Default/gtk/gtkrc"
#define GDM_KEY_ICONWIDTH "gui/MaxIconWidth=128"
#define GDM_KEY_ICONHEIGHT "gui/MaxIconHeight=128"

#define GDM_KEY_BROWSER "greeter/Browser=false"
#define GDM_KEY_EXCLUDE "greeter/Exclude=bin,daemon,adm,lp,sync,shutdown,halt,mail,news,uucp,operator,nobody,gdm,postgres,pvm"
#define GDM_KEY_FACE "greeter/DefaultFace=" EXPANDED_PIXMAPDIR "nobody.png"
#define GDM_KEY_FACEDIR "greeter/GlobalFaceDir=" EXPANDED_DATADIR "/faces/"
#define GDM_KEY_FONT "greeter/Font=-adobe-helvetica-bold-r-normal-*-*-180-*-*-*-*-*-*"
#define GDM_KEY_ICON "greeter/Icon=" EXPANDED_PIXMAPDIR "/gdm.xpm"
#define GDM_KEY_LOCALE "greeter/DefaultLocale=english"
#define GDM_KEY_LOCFILE "greeter/LocaleFile=" EXPANDED_LOCALEDIR "/locale.alias"
#define GDM_KEY_LOGO "greeter/Logo=" EXPANDED_PIXMAPDIR "/gnome-logo-large.png"
#define GDM_KEY_QUIVER "greeter/Quiver=true"
#define GDM_KEY_SYSMENU "greeter/SystemMenu=true"
#define GDM_KEY_CONFIGURATOR "daemon/Configurator=" EXPANDED_GDMCONFIGDIR "/gdmconfig --disable-sound --disable-crash-dialog"
#define GDM_KEY_CONFIG_AVAILABLE "greeter/ConfigAvailable=true"
#define GDM_KEY_TITLE_BAR "greeter/TitleBar=true"
/* translated string gets HATE defaults */
#define GDM_KEY_WELCOME_TR "greeter/Welcome"
#define GDM_KEY_WELCOME "greeter/Welcome=Welcome to %n"
#define GDM_KEY_XINERAMASCREEN "greeter/XineramaScreen=0"
#define GDM_KEY_BACKGROUNDPROG "greeter/BackgroundProgram="
#define GDM_KEY_BACKGROUNDIMAGE "greeter/BackgroundImage="
#define GDM_KEY_BACKGROUNDCOLOR "greeter/BackgroundColor=#007777"
#define GDM_KEY_BACKGROUNDTYPE "greeter/BackgroundType=2"
#define GDM_KEY_BACKGROUNDSCALETOFIT "greeter/BackgroundScaleToFit=true"
#define GDM_KEY_BACKGROUNDREMOTEONLYCOLOR "greeter/BackgroundRemoteOnlyColor=true"
#define GDM_KEY_LOCK_POSITION "greeter/LockPosition=false"
#define GDM_KEY_SET_POSITION "greeter/SetPosition=false"
#define GDM_KEY_POSITIONX "greeter/PositionX=0"
#define GDM_KEY_POSITIONY "greeter/PositionY=0"
#define GDM_KEY_USE_24_CLOCK "greeter/Use24Clock=false"

#define GDM_KEY_SCAN "chooser/ScanTime=3"
#define GDM_KEY_HOST "chooser/DefaultHostImg=" EXPANDED_PIXMAPDIR "/nohost.png"
#define GDM_KEY_HOSTDIR "chooser/HostImageDir=" EXPANDED_DATADIR "/hosts/"
#define GDM_KEY_HOSTS "chooser/Hosts="
#define GDM_KEY_BROADCAST "chooser/Broadcast=true"

#define GDM_KEY_DEBUG "debug/Enable=false"

#define GDM_KEY_SERVERS "servers"

#define GDM_KEY_SHOW_GNOME_CHOOSER "greeter/ShowGnomeChooserSession=true"
#define GDM_KEY_SHOW_GNOME_FAILSAFE "greeter/ShowGnomeFailsafeSession=true"
#define GDM_KEY_SHOW_XTERM_FAILSAFE "greeter/ShowXtermFailsafeSession=true"
#define GDM_KEY_SHOW_LAST_SESSION "greeter/ShowLastSession=true"

#define GDM_SESSION_FAILSAFE_GNOME "GDM_Failsafe.GNOME"
#define GDM_SESSION_FAILSAFE_XTERM "GDM_Failsafe.XTERM"
#define GDM_SESSION_GNOME_CHOOSER "Gnome Chooser"

#define GDM_STANDARD "Standard"

#ifndef TYPEDEF_GDM_CONNECTION
#define TYPEDEF_GDM_CONNECTION
typedef struct _GdmConnection GdmConnection;
#endif  /* TYPEDEF_GDM_CONNECTION */

typedef struct _GdmDisplay GdmDisplay;

/* Use this to get the right authfile name */
#define GDM_AUTHFILE(display) \
	(display->authfile_gdm != NULL ? display->authfile_gdm : display->authfile)

struct _GdmDisplay {
    CARD32 sessionid;
    Display *dsp;
    gchar *authfile; /* authfile for the server */
    gchar *authfile_gdm; /* authfile readable by gdm user
			    if necessary */
    GSList *auths; 
    gchar *userauth;
    gboolean authfb;
    gchar *command;
    gboolean failsafe_xserver;
    gboolean use_chooser;
    guint indirect_id;
    gchar *cookie;
    gchar *bcookie;
    gchar *name;
    gchar *hostname;
    struct in_addr addr;
    guint8 dispstat;
    guint16 dispnum;
    guint8 servstat;
    time_t starttime;
    guint8 type;
    pid_t greetpid;
    pid_t servpid;
    pid_t sesspid;
    pid_t slavepid;
    pid_t chooserpid;
    time_t acctime;

#ifdef __linux__
    int vt;
#endif

    gboolean busy_display;

    gboolean console;

    time_t last_start_time;
    gint retry_count;

    int sleep_before_run;

    time_t last_x_failed;
    int x_faileds;

    gboolean disabled;

    gboolean logged_in; /* TRUE if someone is logged in */
    char *login;

    gboolean timed_login_ok;

    int screenx;
    int screeny;
    int screenwidth; /* Note 0 means use the gdk size */
    int screenheight;

    /* Flexi stuff */
    char *xnest_disp;
    char *xnest_auth_file;
    uid_t server_uid;
    GdmConnection *socket_conn;
};

typedef struct _GdmXServer GdmXServer;
struct _GdmXServer {
	char *id;
	char *name;
	char *command;
	gboolean flexible;
	gboolean choosable; /* not implemented yet */
};

typedef struct _GdmIndirectDisplay GdmIndirectDisplay;
struct _GdmIndirectDisplay {
	int id;
	struct sockaddr_in* dsp_sa;
	time_t acctime;
	struct in_addr *chosen_host;
};

/* NOTE: Timeout and max are hardcoded */
typedef struct _GdmForwardQuery GdmForwardQuery;
struct _GdmForwardQuery {
	time_t acctime;
	struct sockaddr_in* dsp_sa;
	struct sockaddr_in* from_sa;
};
#define GDM_MAX_FORWARD_QUERIES 10
#define GDM_FORWARD_QUERY_TIMEOUT 30

/* some extra xdmcp opcodes that xdm will happily ignore since they'll be
 * the wrong xdmcp version anyway */
#define GDM_XDMCP_PROTOCOL_VERSION 1001
enum {
	GDM_XDMCP_FIRST_OPCODE = 1000, /*just a marker, not an opcode */

	GDM_XDMCP_MANAGED_FORWARD = 1000,
		/* manager (master) -> manager
		 * A packet with MANAGED_FORWARD is sent to the
		 * manager that sent the forward query from the manager to
		 * which forward query was sent.  It indicates that the forward
		 * was fully processed and that the client now has either
		 * a managed session, or has been sent denial, refuse or failed.
		 * (if the denial gets lost then client gets dumped into the
		 * chooser again).  This should be resent a few times
		 * until some (short) timeout or until GOT_MANAGED_FORWARD
		 * is sent.  GDM sends at most 3 packates with 1.5 seconds
		 * between each.
		 *
		 * Argument is ARRAY8 with the address of the originating host */
	GDM_XDMCP_GOT_MANAGED_FORWARD,
		/* manager -> manager (master)
		 * A single packet with GOT_MANAGED_FORWARD is sent to indicate
		 * that we did receive the MANAGED_FORWARD packet.  The argument
		 * must match the MANAGED_FORWARD one or it will just be ignored.
		 *
		 * Argument is ARRAY8 with the address of the originating host */
	GDM_XDMCP_LAST_OPCODE /*just a marker, not an opcode */
};

typedef gboolean (*GSignalFunc) (gint8		signal,
				 gpointer	data);
guint	g_signal_add		(gint8		signal,
				 GSignalFunc	function,
				 gpointer    	data);
guint   g_signal_add_full       (gint           priority,
				 gint8          signal,
				 GSignalFunc    function,
				 gpointer       data,
				 GDestroyNotify destroy);
void    g_signal_notify         (gint8          signal);

/* If id == NULL, then get the first X server */
GdmXServer *	gdm_find_x_server	(const char *id);


void gdm_run (void);
void gdm_quit (void);

/* primitive protocol for controlling the daemon from slave
 * or gdmconfig or whatnot */

/* The ones that pass a <slave pid> must be from a valid slave, and
 * the slave will be sent a SIGUSR2 */
/* The fifo protocol, used only by gdm internally */
#define GDM_SOP_CHOSEN       "CHOSEN" /* <indirect id> <ip addr> */
#define GDM_SOP_XPID         "XPID" /* <slave pid> <xpid> */
#define GDM_SOP_SESSPID      "SESSPID" /* <slave pid> <sesspid> */
#define GDM_SOP_GREETPID     "GREETPID" /* <slave pid> <greetpid> */
#define GDM_SOP_CHOOSERPID   "CHOOSERPID" /* <slave pid> <chooserpid> */
#define GDM_SOP_LOGGED_IN    "LOGGED_IN" /* <slave pid> <logged_in as int> */
#define GDM_SOP_LOGIN        "LOGIN" /* <slave pid> <username> */
#define GDM_SOP_COOKIE       "COOKIE" /* <slave pid> <cookie> */
#define GDM_SOP_DISP_NUM     "DISP_NUM" /* <slave pid> <display as int> */
/* For linux only currently */
#define GDM_SOP_VT_NUM       "VT_NUM" /* <slave pid> <vt as int> */
#define GDM_SOP_FLEXI_ERR    "FLEXI_ERR" /* <slave pid> <error num> */
	/* 3 = X failed */
	/* 4 = X too busy */
	/* 5 = Xnest can't connect */
#define GDM_SOP_FLEXI_OK     "FLEXI_OK" /* <slave pid> */
#define GDM_SOP_SOFT_RESTART "SOFT_RESTART" /* no arguments */
#define GDM_SOP_START_NEXT_LOCAL "START_NEXT_LOCAL" /* no arguments */

#define GDM_SUP_SOCKET "/tmp/.gdm_socket"

/*
 * The user socket protocol.  Each command is given on a separate line
 *
 * A user should first send a VERSION\n after connecting and only do
 * anything else if gdm responds with the correct response.  The version
 * is the gdm version and not a "protocol" revision, so you can't check
 * against a single version but check if the version is higher then some
 * value.
 */
/* The user protocol, using /tmp/.gdm_socket */

#define GDM_SUP_VERSION "VERSION" /* no arguments */
/* VERSION: Query version
 * Supported since: 2.2.4.0
 * Arguments:  None
 * Answers:
 *   GDM <gdm version>
 */
#define GDM_SUP_AUTH_LOCAL "AUTH_LOCAL" /* <xauth cookie> */
/* AUTH_LOCAL: Setup this connection as authenticated for FLEXI_SERVER
 *             Because all full blown (non-Xnest) servers can be started
 *             only from users logged in locally, and here gdm assumes
 *             only users logged in from gdm.  They must pass the xauth
 *             MIT-MAGIC-COOKIE-1 that they were passed before the
 *             connection is authenticated.
 * Supported since: 2.2.4.0
 * Arguments:  <xauth cookie>
 *   <xauth cookie> is in hex form with no 0x prefix
 * Answers:
 *   OK
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      100 = Not authenticated
 *      999 = Unknown error
 */
#define GDM_SUP_FLEXI_XSERVER "FLEXI_XSERVER" /* <xserver type> */
/* FLEXI_XSERVER: Start a new X flexible server
 *   Only supported on connection that passed AUTH_LOCAL
 * Supported since: 2.2.4.0
 * Arguments:  <xserver type>
 *   If no arguments, starts the standard x server
 * Answers:
 *   OK <display>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      1 = No more flexi servers
 *      2 = Startup errors
 *      3 = X failed
 *      4 = X too busy
 *      6 = No server binary
 *      100 = Not authenticated
 *      999 = Unknown error
 */
#define GDM_SUP_FLEXI_XNEST  "FLEXI_XNEST" /* <display> <xauth cookie> <xauth file> */
/* FLEXI_XNEXT: Start a new flexible Xnest server
 * Supported since: 2.2.4.2
 *   Note: supported an older version from 2.2.4.0, but since 2.2.4.2 you must
 *   supply 3 arguments or ERROR 100 will be returned.  This will start Xnest
 *   using the XAUTHORITY file supplied and as the uid same as the owner of that
 *   file.  You Must also supply the cookie as the third argument for this
 *   display, to prove that you indeed are this user.  Also this file must be
 *   readable ONLY by this user, that is have a mode of 0600.  If this all is
 *   not met, ERROR 100 is returned.
 *   Note: The cookie should be the MIT-MAGIC-COOKIE-1, the first one gdm
 *   can find in the XAUTHORITY file for this display.  If that's not what you
 *   use you should generate one first.  The cookie should be in hex form.
 * Arguments:  <display to run on> <xauth cookie for the display> <xauth file>
 * Answers:
 *   OK <display>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      1 = No more flexi servers
 *      2 = Startup errors
 *      3 = X failed
 *      4 = X too busy
 *      5 = Xnest can't connect
 *      6 = No server binary
 *      100 = Not authenticated
 *      999 = Unknown error
 */
#define GDM_SUP_CONSOLE_SERVERS  "CONSOLE_SERVERS" /* None */
/* CONSOLE_SERVERS: List all console servers, useful for linux mostly
 *  Doesn't list xdmcp and xnest non-console servers
 * Supported since: 2.2.4.0
 * Arguments:  None
 * Answers:
 *   OK <server>;<server>;...
 *
 *   <server> is <display>,<logged in user>,<vt or xnest display>
 *
 *   <logged in user> can be empty in case no one logged in yet,
 *   and <vt> can be -1 if it's not known or not supported (on non-linux
 *   for example).  If the display is an xnest display and is a console one
 *   (that is, it is an xnest inside another console display) it is listed
 *   and instead of vt, it lists the parent display in standard form.
 */
#define GDM_SUP_CLOSE        "CLOSE" /* no arguments */
/* CLOSE Answers: None
 * Supported since: 2.2.4.0
 */

/* User flags for the SUP protocol */
enum {
	GDM_SUP_FLAG_AUTHENTICATED = 0x1 /* authenticated as a local user,
					  * from a local display we started */
};

#endif /* GDM_H */

/* EOF */
