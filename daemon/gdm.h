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

#define GDM_MAX_PASS 256	/* Define a value for password length. Glibc
				 * leaves MAX_PASS undefined. */

#define STX 0x2			/* Start of txt */
#define BEL 0x7			/* Bell, used to interrupt login for
				 * say timed login or something similar */

#define TYPE_STATIC 1		/* X server defined in gdm.conf */
#define TYPE_XDMCP 2		/* Remote display/Xserver */
#define TYPE_FLEXI 3		/* Local Flexi X server */
#define TYPE_FLEXI_XNEST 4	/* Local Flexi Xnest server */
#define TYPE_XDMCP_PROXY 5	/* Proxy X server for XDMCP */

#define SERVER_IS_LOCAL(d) ((d)->type == TYPE_STATIC || \
			    (d)->type == TYPE_FLEXI || \
			    (d)->type == TYPE_FLEXI_XNEST || \
			    (d)->type == TYPE_XDMCP_PROXY)
#define SERVER_IS_FLEXI(d) ((d)->type == TYPE_FLEXI || \
			    (d)->type == TYPE_FLEXI_XNEST || \
			    (d)->type == TYPE_XDMCP_PROXY)
#define SERVER_IS_PROXY(d) ((d)->type == TYPE_FLEXI_XNEST || \
			    (d)->type == TYPE_XDMCP_PROXY)
#define SERVER_IS_XDMCP(d) ((d)->type == TYPE_XDMCP || \
			    (d)->type == TYPE_XDMCP_PROXY)

/* These are the servstat values, also used as server
 * process exit codes */
#define SERVER_TIMEOUT 2	/* Server didn't start */
#define SERVER_DEAD 250		/* Server stopped */
#define SERVER_PENDING 251	/* Server started but not ready for connections yet */
#define SERVER_RUNNING 252	/* Server running and ready for connections */
#define SERVER_ABORT 253	/* Server failed badly. Suspending display. */

/* DO NOTE USE 1, that's used as error if x connection fails usually */
/* Note that there is no reason why these were a power of two, and note
 * that they have to fit in 256 */
/* These are the exit codes */
#define DISPLAY_REMANAGE 2	/* Restart display */
#define DISPLAY_ABORT 4		/* Houston, we have a problem */
#define DISPLAY_REBOOT 8	/* Rebewt */
#define DISPLAY_HALT 16		/* Halt */
#define DISPLAY_SUSPEND 17	/* Suspend (don't use, use the interrupt) */
#define DISPLAY_CHOSEN 20	/* successful chooser session,
				   restart display */
#define DISPLAY_RUN_CHOOSER 30	/* Run chooser */
#define DISPLAY_XFAILED 64	/* X failed */
#define DISPLAY_GREETERFAILED 65 /* greeter failed (crashed) */
#define DISPLAY_RESTARTGREETER 127 /* Restart greeter */
#define DISPLAY_RESTARTGDM 128	/* Restart GDM */

enum {
	DISPLAY_UNBORN /* Not yet started */,
	DISPLAY_ALIVE /* Yay! we're alive (non-XDMCP) */,
	XDMCP_PENDING /* Pending XDMCP display */,
	XDMCP_MANAGED /* Managed XDMCP display */,
	DISPLAY_DEAD /* Left for dead */,
	DISPLAY_CONFIG /* in process of being configured */
};

/* Opcodes for the highly sophisticated protocol used for
 * daemon<->greeter communications */

/* This will change if there are incompatible
 * protocol changes */
#define GDM_GREETER_PROTOCOL_VERSION "3"

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
#define GDM_STARTTIMER 's'
#define GDM_STOPTIMER  'S'
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
#define GDM_NOFOCUS    'f' /* Don't focus the login window (optional) */
#define GDM_FOCUS      'F' /* Allow focus on the login window again (optional) */
#define GDM_SAVEDIE    '!' /* Save wm order and die (and set busy cursor) */
#define GDM_QUERY_CAPSLOCK 'Q' /* Is capslock on? */

/* Different login interruptions */
#define GDM_INTERRUPT_TIMED_LOGIN 'T'
#define GDM_INTERRUPT_CONFIGURE   'C'
#define GDM_INTERRUPT_SUSPEND     'S'
#define GDM_INTERRUPT_SELECT_USER 'U'
#define GDM_INTERRUPT_LOGIN_SOUND 'L'
#define GDM_INTERRUPT_THEME       'H'
#define GDM_INTERRUPT_CANCEL      'X'

/* List delimiter for config file lists */
#define GDM_DELIMITER_MODULES ":"
#define GDM_DELIMITER_THEMES "/:"

/* The dreaded miscellaneous category */
#define FIELD_SIZE 256
#define PIPE_SIZE 4096

/* Configuration constants */
#define GDM_KEY_CHOOSER "daemon/Chooser=" EXPANDED_LIBEXECDIR "/gdmchooser"
/* This defaults to true for backward compatibility,
 * it will not actually do automatic login since the AutomaticLogin defaults
 * to nothing */
#define GDM_KEY_AUTOMATICLOGIN_ENABLE "daemon/AutomaticLoginEnable=true"
#define GDM_KEY_AUTOMATICLOGIN "daemon/AutomaticLogin="
#define GDM_KEY_ALWAYSRESTARTSERVER "daemon/AlwaysRestartServer=false"
#define GDM_KEY_GREETER "daemon/Greeter=" EXPANDED_LIBEXECDIR "/gdmlogin"
#define GDM_KEY_REMOTEGREETER "daemon/RemoteGreeter=" EXPANDED_LIBEXECDIR "/gdmlogin"
#define GDM_KEY_ADD_GTK_MODULES "daemon/AddGtkModules=false"
#define GDM_KEY_GTK_MODULES_LIST "daemon/GtkModulesList="
#define GDM_KEY_GROUP "daemon/Group=gdm"
#define GDM_KEY_HALT "daemon/HaltCommand=" HALT_COMMAND
#define GDM_KEY_INITDIR "daemon/DisplayInitDir=" EXPANDED_SYSCONFDIR "/gdm/Init"
#define GDM_KEY_KILLIC "daemon/KillInitClients=true"
#define GDM_KEY_LOGDIR "daemon/LogDir=" EXPANDED_LOGDIR
#define GDM_KEY_PATH "daemon/DefaultPath=" GDM_USER_PATH
#define GDM_KEY_PIDFILE "daemon/PidFile=/var/run/gdm.pid"
#define GDM_KEY_POSTSESS "daemon/PostSessionScriptDir=" EXPANDED_SYSCONFDIR "/gdm/PostSession/"
#define GDM_KEY_PRESESS "daemon/PreSessionScriptDir=" EXPANDED_SYSCONFDIR "/gdm/PreSession/"
#define GDM_KEY_POSTLOGIN "daemon/PostLoginScriptDir=" EXPANDED_SYSCONFDIR "/gdm/PreSession/"
#define GDM_KEY_FAILSAFE_XSERVER "daemon/FailsafeXServer="
#define GDM_KEY_XKEEPSCRASHING "daemon/XKeepsCrashing=" EXPANDED_SYSCONFDIR "/gdm/XKeepsCrashing"
#define GDM_KEY_REBOOT  "daemon/RebootCommand=" REBOOT_COMMAND
#define GDM_KEY_ROOTPATH "daemon/RootPath=/sbin:/usr/sbin:" GDM_USER_PATH
#define GDM_KEY_SERVAUTH "daemon/ServAuthDir=" EXPANDED_AUTHDIR
#define GDM_KEY_SESSDIR "daemon/SessionDesktopDir=/etc/X11/sessions/:" EXPANDED_SYSCONFDIR "/dm/Sessions/:" EXPANDED_DATADIR "/gdm/BuiltInSessions/:" EXPANDED_DATADIR "/xsessions/"
#define GDM_KEY_BASEXSESSION "daemon/BaseXsession=" EXPANDED_SYSCONFDIR "/gdm/Xsession"
#define GDM_KEY_DEFAULTSESSION "daemon/DefaultSession=gnome.desktop"
#define GDM_KEY_SUSPEND "daemon/SuspendCommand=" SUSPEND_COMMAND

#define GDM_KEY_UAUTHDIR "daemon/UserAuthDir="
#define GDM_KEY_UAUTHFB "daemon/UserAuthFBDir=/tmp"
#define GDM_KEY_UAUTHFILE "daemon/UserAuthFile=.Xauthority"
#define GDM_KEY_USER "daemon/User=gdm"
#define GDM_KEY_CONSOLE_NOTIFY "daemon/ConsoleNotify=true"

#define GDM_KEY_DOUBLELOGINWARNING "daemon/DoubleLoginWarning=true"
#define GDM_KEY_ALWAYS_LOGIN_CURRENT_SESSION "daemon/AlwaysLoginCurrentSession=false"

#define GDM_KEY_DISPLAY_LAST_LOGIN "daemon/DisplayLastLogin=false"

/* This defaults to true for backward compatibility,
 * it will not actually do timed login since the TimedLogin defaults
 * to nothing */
#define GDM_KEY_TIMED_LOGIN_ENABLE "daemon/TimedLoginEnable=true"
#define GDM_KEY_TIMED_LOGIN "daemon/TimedLogin="
#define GDM_KEY_TIMED_LOGIN_DELAY "daemon/TimedLoginDelay=30"

#define GDM_KEY_FLEXI_REAP_DELAY_MINUTES "daemon/FlexiReapDelayMinutes=5"

#define GDM_KEY_STANDARD_XSERVER "daemon/StandardXServer=" X_SERVER
#define GDM_KEY_FLEXIBLE_XSERVERS "daemon/FlexibleXServers=5"
#define GDM_KEY_DYNAMIC_XSERVERS "daemon/DynamicXServers=false"
#define GDM_KEY_XNEST "daemon/Xnest=" X_SERVER_PATH "/Xnest -name Xnest"
/* Keys for automatic VT allocation rather then letting it up to the
 * X server */
#define GDM_KEY_FIRSTVT "daemon/FirstVT=7"
#define GDM_KEY_VTALLOCATION "daemon/VTAllocation=true"

#define GDM_KEY_CONSOLE_CANNOT_HANDLE "daemon/ConsoleCannotHandle=am,ar,az,bn,el,fa,gu,hi,ja,ko,ml,mr,pa,ta,zh"

/* Per server definitions */
#define GDM_KEY_SERVER_PREFIX "server-"
#define GDM_KEY_SERVER_NAME "name=Standard server"
#define GDM_KEY_SERVER_COMMAND "command=" X_SERVER
/* runnable as flexi server */
#define GDM_KEY_SERVER_FLEXIBLE "flexible=true"
/* choosable from the login screen */
#define GDM_KEY_SERVER_CHOOSABLE "choosable=false"
/* Login is handled by gdm, otherwise it's a remote server */
#define GDM_KEY_SERVER_HANDLED "handled=true"
/* Instead of the greeter run the chooser */
#define GDM_KEY_SERVER_CHOOSER "chooser=false"

#define GDM_KEY_ALLOWROOT "security/AllowRoot=true"
#define GDM_KEY_ALLOWREMOTEROOT "security/AllowRemoteRoot=true"
#define GDM_KEY_ALLOWREMOTEAUTOLOGIN "security/AllowRemoteAutoLogin=false"
#define GDM_KEY_MAXFILE "security/UserMaxFile=65536"
#define GDM_KEY_RELAXPERM "security/RelaxPermissions=0"
#define GDM_KEY_CHECKDIROWNER "security/CheckDirOwner=true"
#define GDM_KEY_RETRYDELAY "security/RetryDelay=1"
#define GDM_KEY_DISALLOWTCP "security/DisallowTCP=true"

#define GDM_KEY_NEVERPLACECOOKIESONNFS "security/NeverPlaceCookiesOnNFS=true"

#define GDM_KEY_XDMCP "xdmcp/Enable=false"
#define GDM_KEY_MAXPEND "xdmcp/MaxPending=4"
#define GDM_KEY_MAXSESS "xdmcp/MaxSessions=16"
#define GDM_KEY_MAXWAIT "xdmcp/MaxWait=15"
#define GDM_KEY_DISPERHOST "xdmcp/DisplaysPerHost=2"
#define GDM_KEY_UDPPORT "xdmcp/Port=177"
#define GDM_KEY_INDIRECT "xdmcp/HonorIndirect=true"
#define GDM_KEY_MAXINDIR "xdmcp/MaxPendingIndirect=4"
#define GDM_KEY_MAXINDWAIT "xdmcp/MaxWaitIndirect=15"
#define GDM_KEY_PINGINTERVAL "xdmcp/PingIntervalSeconds=15"
#define GDM_KEY_WILLING "xdmcp/Willing=" EXPANDED_SYSCONFDIR "/gdm/Xwilling"

#define GDM_KEY_XDMCP_PROXY "xdmcp/EnableProxy=false"
#define GDM_KEY_XDMCP_PROXY_XSERVER "xdmcp/ProxyXServer="
#define GDM_KEY_XDMCP_PROXY_RECONNECT "xdmcp/ProxyReconnect="

#define GDM_KEY_GTK_THEME "gui/GtkTheme=Default"
#define GDM_KEY_GTKRC "gui/GtkRC="
#define GDM_KEY_ICONWIDTH "gui/MaxIconWidth=128"
#define GDM_KEY_ICONHEIGHT "gui/MaxIconHeight=128"

#define GDM_KEY_ALLOW_GTK_THEME_CHANGE "gui/AllowGtkThemeChange=true"
#define GDM_KEY_GTK_THEMES_TO_ALLOW "gui/GtkThemesToAllow=all"

#define GDM_KEY_BROWSER "greeter/Browser=false"
#define GDM_KEY_INCLUDE "greeter/Include="
#define GDM_KEY_EXCLUDE "greeter/Exclude=bin,daemon,adm,lp,sync,shutdown,halt,mail,news,uucp,operator,nobody,gdm,postgres,pvm,rpm,nfsnobody,pcap"
#define GDM_KEY_INCLUDEALL "greeter/IncludeAll=false"
#define GDM_KEY_MINIMALUID "greeter/MinimalUID=100"
#define GDM_KEY_FACE "greeter/DefaultFace=" EXPANDED_PIXMAPDIR "/nobody.png"
#define GDM_KEY_FACEDIR "greeter/GlobalFaceDir=" EXPANDED_DATADIR "/pixmaps/faces/"
#define GDM_KEY_LOCFILE "greeter/LocaleFile=" EXPANDED_LOCALEDIR "/locale.alias"
#define GDM_KEY_LOGO "greeter/Logo=" EXPANDED_PIXMAPDIR "/gdm-foot-logo.png"
#define GDM_KEY_QUIVER "greeter/Quiver=true"
#define GDM_KEY_SYSMENU "greeter/SystemMenu=true"
#define GDM_KEY_CONFIGURATOR "daemon/Configurator=" EXPANDED_SBINDIR "/gdmsetup --disable-sound --disable-crash-dialog"
#define GDM_KEY_CONFIG_AVAILABLE "greeter/ConfigAvailable=true"
#define GDM_KEY_CHOOSER_BUTTON "greeter/ChooserButton=true"
#define GDM_KEY_TITLE_BAR "greeter/TitleBar=true"
#define GDM_KEY_DEFAULT_WELCOME "greeter/DefaultWelcome=true"
#define GDM_KEY_DEFAULT_WELCOME_BACKTEST "greeter/DefaultWelcome="
#define GDM_KEY_DEFAULT_REMOTEWELCOME "greeter/DefaultRemoteWelcome=true"
#define GDM_KEY_DEFAULT_REMOTEWELCOME_BACKTEST "greeter/DefaultRemoteWelcome="
#define GDM_KEY_WELCOME "greeter/Welcome="
#define GDM_KEY_REMOTEWELCOME "greeter/RemoteWelcome="
#define GDM_KEY_XINERAMASCREEN "greeter/XineramaScreen=0"
#define GDM_KEY_BACKGROUNDPROG "greeter/BackgroundProgram="
#define GDM_KEY_RUNBACKGROUNDPROGALWAYS "greeter/RunBackgroundProgramAlways=false"
#define GDM_KEY_BACKGROUNDPROGINITIALDELAY "greeter/BackgroundProgramInitialDelay=30"
#define GDM_KEY_RESTARTBACKGROUNDPROG "greeter/RestartBackgroundProgram=true"
#define GDM_KEY_BACKGROUNDPROGRESTARTDELAY "greeter/BackgroundProgramRestartDelay=30"
#define GDM_KEY_BACKGROUNDIMAGE "greeter/BackgroundImage="
#define GDM_KEY_BACKGROUNDCOLOR "greeter/BackgroundColor=#76848F"
#define GDM_KEY_BACKGROUNDTYPE "greeter/BackgroundType=2"
#define GDM_KEY_BACKGROUNDSCALETOFIT "greeter/BackgroundScaleToFit=true"
#define GDM_KEY_BACKGROUNDREMOTEONLYCOLOR "greeter/BackgroundRemoteOnlyColor=true"
#define GDM_KEY_LOCK_POSITION "greeter/LockPosition=false"
#define GDM_KEY_SET_POSITION "greeter/SetPosition=false"
#define GDM_KEY_POSITIONX "greeter/PositionX=0"
#define GDM_KEY_POSITIONY "greeter/PositionY=0"
#define GDM_KEY_USE_24_CLOCK "greeter/Use24Clock=false"
#define GDM_KEY_ENTRY_CIRCLES "greeter/UseCirclesInEntry=false"
#define GDM_KEY_ENTRY_INVISIBLE "greeter/UseInvisibleInEntry=false"
#define GDM_KEY_GRAPHICAL_THEME "greeter/GraphicalTheme=circles"
#define GDM_KEY_GRAPHICAL_THEMES "greeter/GraphicalThemes=circles"
#define GDM_KEY_GRAPHICAL_THEME_RAND "greeter/GraphicalThemeRand=false"
#define GDM_KEY_GRAPHICAL_THEME_DIR "greeter/GraphicalThemeDir=" EXPANDED_DATADIR "/gdm/themes/"

#define GDM_KEY_INFO_MSG_FILE "greeter/InfoMsgFile="
#define GDM_KEY_INFO_MSG_FONT "greeter/InfoMsgFont="

#define GDM_KEY_SOUND_ON_LOGIN_READY "greeter/SoundOnLogin=true"
#define GDM_KEY_SOUND_ON_LOGIN_SUCCESS "greeter/SoundOnLoginSuccess=false"
#define GDM_KEY_SOUND_ON_LOGIN_FAILURE "greeter/SoundOnLoginFailure=false"
#define GDM_KEY_SOUND_ON_LOGIN_READY_FILE "greeter/SoundOnLoginFile="
#define GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE "greeter/SoundOnLoginSuccessFile="
#define GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE "greeter/SoundOnLoginFailureFile="
#define GDM_KEY_SOUND_PROGRAM "daemon/SoundProgram=/usr/bin/play"

#define GDM_KEY_SCAN "chooser/ScanTime=4"
#define GDM_KEY_HOST "chooser/DefaultHostImg=" EXPANDED_PIXMAPDIR "/nohost.png"
#define GDM_KEY_HOSTDIR "chooser/HostImageDir=" EXPANDED_DATADIR "/hosts/"
#define GDM_KEY_HOSTS "chooser/Hosts="
#ifdef ENABLE_IPV6
#define GDM_KEY_MULTICAST "chooser/Multicast=true"
#define GDM_KEY_MULTICAST_ADDR "chooser/MulticastAddr=ff02::1"
#endif
#define GDM_KEY_BROADCAST "chooser/Broadcast=true"
#define GDM_KEY_ALLOWADD "chooser/AllowAdd=true"

#define GDM_KEY_DEBUG "debug/Enable=false"

#define GDM_KEY_SERVERS "servers"

#define GDM_KEY_SHOW_GNOME_FAILSAFE "greeter/ShowGnomeFailsafeSession=true"
#define GDM_KEY_SHOW_XTERM_FAILSAFE "greeter/ShowXtermFailsafeSession=true"
#define GDM_KEY_SHOW_LAST_SESSION "greeter/ShowLastSession=true"

#define GDM_SESSION_FAILSAFE_GNOME "GDM_Failsafe.GNOME"
#define GDM_SESSION_FAILSAFE_XTERM "GDM_Failsafe.XTERM"

/* FIXME: will support these builtin types later */
#define GDM_SESSION_DEFAULT "default"
#define GDM_SESSION_CUSTOM "custom"
#define GDM_SESSION_FAILSAFE "failsafe"

#define GDM_STANDARD "Standard"

#define GDM_DEFAULT_WELCOME_MSG "Welcome"
#define GDM_DEFAULT_REMOTEWELCOME_MSG "Welcome to %n"

#define GDM_RESPONSE_CANCEL "GDM_RESPONSE_CANCEL"

#ifndef TYPEDEF_GDM_CONNECTION
#define TYPEDEF_GDM_CONNECTION
typedef struct _GdmConnection GdmConnection;
#endif  /* TYPEDEF_GDM_CONNECTION */

typedef enum {
	GDM_LOGOUT_ACTION_NONE = 0,
	GDM_LOGOUT_ACTION_HALT,
	GDM_LOGOUT_ACTION_REBOOT,
	GDM_LOGOUT_ACTION_SUSPEND,
	GDM_LOGOUT_ACTION_LAST
} GdmLogoutAction;

#ifndef TYPEDEF_GDM_DISPLAY
#define TYPEDEF_GDM_DISPLAY
typedef struct _GdmDisplay GdmDisplay;
#endif /* TYPEDEF_GDM_DISPLAY */

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
    GSList *local_auths; 
    gchar *userauth;
    gboolean authfb;
    time_t last_auth_touch;
    gchar *command;
    gboolean failsafe_xserver;
    gboolean use_chooser; /* run chooser instead of greeter */
    gchar *chosen_hostname; /* locally chosen hostname if not NULL,
			       "-query chosen_hostname" is appened to server command line */
    guint indirect_id;
    gchar *cookie;
    gchar *bcookie;
    gchar *name;
    gchar *hostname;
    struct in_addr addr;
#ifdef ENABLE_IPV6
    struct in6_addr addr6;
    struct sockaddr_storage *addrs; /* array of addresses */
#else
    struct in_addr *addrs; /* array of addresses */
#endif
    int addrtype;        /* Specifying the variable used, addr or addr6  */
    int addr_count; /* number of addresses in array */
    /* Note that the above may in fact be empty even though
       addr is set, these are just extra addresses
       (it could also contain addr for all we know) */

    guint8 dispstat;
    guint16 dispnum;
	gboolean removeconf;
    guint8 servstat;
    time_t starttime;
    time_t managetime;
    guint8 type;
    pid_t greetpid;
    pid_t servpid;
    pid_t sesspid;
    int last_sess_status; /* status returned by last session */
    pid_t slavepid;
    pid_t chooserpid;
    time_t acctime;

    gboolean handled;
    gboolean tcp_disallowed;

    int vt;

    gboolean busy_display;

    gboolean attached;  /* Display is physically attached to the machine. */
                        /* TYPE_XDMCP would have this FALSE, eg. */

    time_t last_start_time;
    time_t last_loop_start_time;
    gint retry_count;

    int sleep_before_run;

    time_t last_x_failed;
    int x_faileds;

    gboolean try_different_greeter;

    gboolean logged_in; /* TRUE if someone is logged in */
    char *login;

    gboolean timed_login_ok;

    int screenx;
    int screeny;
    int screenwidth; /* Note 0 means use the gdk size */
    int screenheight;
    int lrh_offsetx; /* lower right hand corner x offset */
    int lrh_offsety; /* lower right hand corner y offset */

    /* Flexi stuff */
    char *parent_disp;
    Display *parent_dsp;
    char *parent_auth_file;
    char *parent_temp_auth_file;
    uid_t server_uid;
    GdmConnection *socket_conn;

    int xdmcp_dispnum;

    /* Notification connection */
    int master_notify_fd;  /* write part of the connection */
    int slave_notify_fd; /* read part of the connection */

    /* order in the Xservers file for sessreg, -1 if unset yet */
    int x_servers_order;

    /* The xsession-errors connection */
    int xsession_errors_fd; /* write to the file */
    int session_output_fd; /* read from the session */
    int xsession_errors_bytes;
#define MAX_XSESSION_ERRORS_BYTES (80*2500)  /* maximum number of bytes in
						the ~/.xsession-errors file */
    char *xsession_errors_filename; /* if NULL then there is no .xsession-errors
				       file */

    int chooser_output_fd; /* from the chooser */
    char *chooser_last_line;

    /* Only set in the main daemon as that's the only place that cares */
    GdmLogoutAction logout_action;
    
    char *theme_name;
};

typedef struct _GdmXServer GdmXServer;
struct _GdmXServer {
	char *id;
	char *name;
	char *command;
	gboolean flexible;
	gboolean choosable; /* not implemented yet */
	gboolean chooser; /* instead of greeter, run chooser */
	gboolean handled;
};

typedef struct _GdmIndirectDisplay GdmIndirectDisplay;
struct _GdmIndirectDisplay {
	int id;
#ifdef ENABLE_IPV6
       struct sockaddr_storage* dsp_sa;
       struct in6_addr* chosen_host6;
#else
	struct sockaddr_in* dsp_sa;
#endif
	time_t acctime;
	struct in_addr *chosen_host;
};

/* NOTE: Timeout and max are hardcoded */
typedef struct _GdmForwardQuery GdmForwardQuery;
struct _GdmForwardQuery {
	time_t acctime;
#ifdef ENABLE_IPV6
       struct sockaddr_storage* dsp_sa;
       struct sockaddr_storage* from_sa;
#else
	struct sockaddr_in* dsp_sa;
	struct sockaddr_in* from_sa;
#endif
};
#define GDM_MAX_FORWARD_QUERIES 10
#define GDM_FORWARD_QUERY_TIMEOUT 30

/* some extra XDMCP opcodes that xdm will happily ignore since they'll be
 * the wrong XDMCP version anyway */
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

/* If id == NULL, then get the first X server */
GdmXServer *	gdm_find_x_server	(const char *id);
void		gdm_final_cleanup	(void);


/* primitive protocol for controlling the daemon from slave
 * or gdmconfig or whatnot */

/* The ones that pass a <slave pid> must be from a valid slave, and
 * the slave will be sent a SIGUSR2.  Nowdays there is a pipe that is
 * used from inside slaves, so those messages may stop being processed
 * by the fifo at some point perhaps.  */
/* The fifo protocol, used only by gdm internally */
#define GDM_SOP_CHOSEN       "CHOSEN" /* <indirect id> <ip addr> */
#define GDM_SOP_CHOSEN_LOCAL "CHOSEN_LOCAL" /* <slave pid> <hostname> */
#define GDM_SOP_XPID         "XPID" /* <slave pid> <xpid> */
#define GDM_SOP_SESSPID      "SESSPID" /* <slave pid> <sesspid> */
#define GDM_SOP_GREETPID     "GREETPID" /* <slave pid> <greetpid> */
#define GDM_SOP_CHOOSERPID   "CHOOSERPID" /* <slave pid> <chooserpid> */
#define GDM_SOP_LOGGED_IN    "LOGGED_IN" /* <slave pid> <logged_in as int> */
#define GDM_SOP_LOGIN        "LOGIN" /* <slave pid> <username> */
#define GDM_SOP_COOKIE       "COOKIE" /* <slave pid> <cookie> */
#define GDM_SOP_AUTHFILE     "AUTHFILE" /* <slave pid> <authfile> */
#define GDM_SOP_QUERYLOGIN   "QUERYLOGIN" /* <slave pid> <username> */
/* if user already logged in somewhere, the ack response will be
   <display>,<migratable>,<display>,<migratable>,... */
#define GDM_SOP_MIGRATE      "MIGRATE" /* <slave pid> <display> */
#define GDM_SOP_DISP_NUM     "DISP_NUM" /* <slave pid> <display as int> */
/* For Linux only currently */
#define GDM_SOP_VT_NUM       "VT_NUM" /* <slave pid> <vt as int> */
#define GDM_SOP_FLEXI_ERR    "FLEXI_ERR" /* <slave pid> <error num> */
	/* 3 = X failed */
	/* 4 = X too busy */
	/* 5 = Xnest can't connect */
#define GDM_SOP_FLEXI_OK     "FLEXI_OK" /* <slave pid> */
#define GDM_SOP_SOFT_RESTART "SOFT_RESTART" /* no arguments */
#define GDM_SOP_START_NEXT_LOCAL "START_NEXT_LOCAL" /* no arguments */
#define GDM_SOP_HUP_ALL_GREETERS "HUP_ALL_GREETERS" /* no arguments */

/* stop waiting for this and go on with your life, useful with
   the --wait-for-go command line option */
#define GDM_SOP_GO "GO" /* no arguments */

/* sometimes we can't do a syslog so we tell the main daemon */
#define GDM_SOP_SYSLOG "SYSLOG" /* <pid> <type> <message> */

/* write out a sessreg (xdm) compatible Xservers file
 * in the ServAuthDir as <name>.Xservers */
#define GDM_SOP_WRITE_X_SERVERS "WRITE_X_SERVERS" /* <slave pid> */

/* All X servers should be restarted rather then regenerated.  Useful
 * if you have updated the X configuration.  Note that this happens
 * only when the user logs out or when we otherwise would have restarted
 * a server, nothing is done by this command. */
#define GDM_SOP_DIRTY_SERVERS "DIRTY_SERVERS"  /* no arguments */

/* restart all servers that people aren't logged in on.  Maybe you may not
 * want to do this on every change of X server config since this may cause
 * flicker on screen and jumping around on the vt.  Perhaps useful to do
 * by asking the user if they want to do that.  Note that this will not
 * kill any logged in sessions. */
#define GDM_SOP_SOFT_RESTART_SERVERS "SOFT_RESTART_SERVERS"  /* no arguments */
/* Suspend the machine if it is even allowed */
#define GDM_SOP_SUSPEND_MACHINE "SUSPEND_MACHINE"  /* no arguments */
#define GDM_SOP_CHOSEN_THEME "CHOSEN_THEME"  /* <slave pid> <theme name> */

/* Start a new standard X flexible server */
#define GDM_SOP_FLEXI_XSERVER "FLEXI_XSERVER" /* no arguments */

/* Notification protocol */
/* keys */
#define GDM_NOTIFY_ALLOWREMOTEROOT "AllowRemoteRoot" /* <true/false as int> */
#define GDM_NOTIFY_ALLOWROOT "AllowRoot" /* <true/false as int> */
#define GDM_NOTIFY_ALLOWREMOTEAUTOLOGIN "AllowRemoteAutoLogin" /* <true/false as int> */
#define GDM_NOTIFY_SYSMENU "SystemMenu" /* <true/false as int> */
#define GDM_NOTIFY_CONFIG_AVAILABLE "ConfigAvailable" /* <true/false as int> */
#define GDM_NOTIFY_CHOOSER_BUTTON "ChooserButton" /* <true/false as int> */
#define GDM_NOTIFY_RETRYDELAY "RetryDelay" /* <seconds> */
#define GDM_NOTIFY_GREETER "Greeter" /* <greeter binary> */
#define GDM_NOTIFY_REMOTEGREETER "RemoteGreeter" /* <greeter binary> */
#define GDM_NOTIFY_TIMED_LOGIN "TimedLogin" /* <login> */
#define GDM_NOTIFY_TIMED_LOGIN_DELAY "TimedLoginDelay" /* <seconds> */
#define GDM_NOTIFY_DISALLOWTCP "DisallowTCP" /* <true/false as int> */
#define GDM_NOTIFY_SOUND_ON_LOGIN_READY_FILE "SoundOnLoginFile" /* <sound file> */
#define GDM_NOTIFY_SOUND_ON_LOGIN_SUCCESS_FILE "SoundOnLoginSuccessFile" /* <sound file> */
#define GDM_NOTIFY_SOUND_ON_LOGIN_FAILURE_FILE "SoundOnLoginFailureFile" /* <sound file> */
#define GDM_NOTIFY_ADD_GTK_MODULES "AddGtkModules" /* <true/false as int> */
#define GDM_NOTIFY_GTK_MODULES_LIST "GtkModulesList" /* <modules list> */

/* commands, seel GDM_SLAVE_NOTIFY_COMMAND */
#define GDM_NOTIFY_DIRTY_SERVERS "DIRTY_SERVERS"
#define GDM_NOTIFY_SOFT_RESTART_SERVERS "SOFT_RESTART_SERVERS"
#define GDM_NOTIFY_GO "GO"
#define GDM_NOTIFY_TWIDDLE_POINTER "TWIDDLE_POINTER"

/* Ack for a slave message */
/* Note that an extra response can follow an 'ack' */
#define GDM_SLAVE_NOTIFY_ACK 'A'
/* Update this key */
#define GDM_SLAVE_NOTIFY_KEY '!'
/* notify a command */
#define GDM_SLAVE_NOTIFY_COMMAND '#'

#define GDM_SUP_SOCKET "/tmp/.gdm_socket"

/*
 * The user socket protocol.  Each command is given on a separate line
 *
 * A user should first send a VERSION\n after connecting and only do
 * anything else if gdm responds with the correct response.  The version
 * is the gdm version and not a "protocol" revision, so you can't check
 * against a single version but check if the version is higher then some
 * value.
 *
 * You can only send a few commands at a time, so if you keep getting error
 * 200 try opening a new socket for every command you send.
 */
/* The user protocol, using /tmp/.gdm_socket */

#define GDM_SUP_VERSION "VERSION" /* no arguments */
/* VERSION: Query version
 * Supported since: 2.2.4.0
 * Arguments:  None
 * Answers:
 *   GDM <gdm version>
 *   ERROR <err number> <english error description>
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_AUTH_LOCAL "AUTH_LOCAL" /* <xauth cookie> */
/* AUTH_LOCAL: Setup this connection as authenticated for FLEXI_SERVER
 *             Because all full blown (non-Xnest) servers can be started
 *             only from users logged in locally, and here gdm assumes
 *             only users logged in from gdm.  They must pass the xauth
 *             MIT-MAGIC-COOKIE-1 that they were passed before the
 *             connection is authenticated.
 *             Note that since 2.6.0.6 you can also use a global
 *             <ServAuthDir>/.cookie, which works for all authentication
 *             except for SET_LOGOUT_ACTION and QUERY_LOGOUT_ACTION
 *             and SET_SAFE_LOGOUT_ACTION which require a logged in
 *             display
 * Supported since: 2.2.4.0
 * Arguments:  <xauth cookie>
 *   <xauth cookie> is in hex form with no 0x prefix
 * Answers:
 *   OK
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      100 = Not authenticated
 *      200 = Too many messages
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
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_FLEXI_XNEST  "FLEXI_XNEST" /* <display> <uid> <xauth cookie> <xauth file> */
/* FLEXI_XNEXT: Start a new flexible Xnest server
 * Supported since: 2.3.90.4
 *   Note: supported an older version from 2.2.4.0, later 2.2.4.2, but
 *   since 2.3.90.4 you must supply 4 arguments or ERROR 100 will be returned.
 *   This will start Xnest  using the XAUTHORITY file supplied and as the
 *   uid same as the owner of that file (and same as you supply).  You must
 *   also supply the cookie as the third argument for this
 *   display, to prove that you indeed are this user.  Also this file must be
 *   readable ONLY by this user, that is have a mode of 0600.  If this all is
 *   not met, ERROR 100 is returned.
 *   Note: The cookie should be the MIT-MAGIC-COOKIE-1, the first one gdm
 *   can find in the XAUTHORITY file for this display.  If that's not what you
 *   use you should generate one first.  The cookie should be in hex form.
 * Arguments:  <display to run on> <uid of requesting user> <xauth cookie for the display> <xauth file>
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
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_ATTACHED_SERVERS "ATTACHED_SERVERS" /* None */
#define GDM_SUP_CONSOLE_SERVERS  "CONSOLE_SERVERS"  /* None */
/* ATTACHED_SERVERS: List all attached servers, useful for Linux mostly
 *   Doesn't list XDMCP and xnest non-attached servers
 * CONSOLE_SERVERS supported, but deprecated due to terminology
 * Supported since: 2.2.4.0
 * Note: This command used to be named CONSOLE_SERVERS, which is still recognized
 *       for backwards compatibility.  The optional pattern argument is supported
 *       as of version 2.8.0.0.
 * Arguments:  <pattern> (optional)
 *   With no argument, all dynamic displays are returned. The optional
 *   <pattern> is a string that may contain glob characters '*', '?', and
 *   '[]'. Only displays that match the pattern will be returned.
 * Answers:
 *   OK <server>;<server>;...
 *
 *   <server> is <display>,<logged in user>,<vt or xnest display>
 *
 *   <logged in user> can be empty in case no one logged in yet,
 *   and <vt> can be -1 if it's not known or not supported (on non-Linux
 *   for example).  If the display is an xnest display and is an attached one
 *   (that is, it is an xnest inside another attached display) it is listed
 *   and instead of vt, it lists the parent display in standard form.
 *
 *   ERROR <err number> <english error description>
 *      1 = Not implemented
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_ALL_SERVERS  "ALL_SERVERS" /* None */
/* ALL_SERVERS: List all servers, including attached, remote, xnest.  This
 * Can for example be useful to figure out if the server you are on is managed
 * by the gdm daemon, by seeing if it is in the list.  It is also somewhat
 * like the 'w' command but for graphical sessions.
 * Supported since: 2.4.2.96
 * Arguments:  None
 * Answers:
 *   OK <server>;<server>;...
 *
 *   <server> is <display>,<logged in user>
 *
 *   <logged in user> can be empty in case no one logged in yet
 *
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_GET_CONFIG "GET_CONFIG" /* <key> */
/* GET_CONFIG:  Get configuration value for key.  Useful so
 * that other programs can request configuration information
 * from GDM.  Any key defined as GDM_KEY_* in gdm.h is 
 * supported.
 * Supported since: 2.6.0.9
 * Arguments:   <key>
 * Answers:
 *   OK <value>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      50 = Unsupported key
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_GET_CONFIG_FILE  "GET_CONFIG_FILE" /* None */
/* GET_CONFIG_FILE: Get config file location being used by
 # the daemon.
 * Supported since: 2.8.0.2
 * Arguments:  None
 * Answers:
 *   OK <full path to GDM configuration file>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_UPDATE_CONFIG "UPDATE_CONFIG" /* <key> */
/* UPDATE_CONFIG: Tell the daemon to update config of some key.  Any user
 *             can really request that values are re-read but the daemon
 *             caches the last date of the config file so a user can't
 *             actually change any values unless they can write the
 *             config file.  The keys that are currently supported are:
 *   		 security/AllowRoot (2.3.90.2)
 *   		 security/AllowRemoteRoot (2.3.90.2)
 *   		 security/AllowRemoteAutoLogin (2.3.90.2)
 *   		 security/RetryDelay (2.3.90.2)
 *   		 security/DisallowTCP (2.4.2.0)
 *   		 daemon/Greeter (2.3.90.2)
 *   		 daemon/RemoteGreeter (2.3.90.2)
 *   		 xdmcp/Enable (2.3.90.2)
 *   		 xdmcp/Port (2.3.90.2)
 *   		 xdmcp/PARAMETERS (2.3.90.2) (pseudokey, all the parameters)
 *			xdmcp/MaxPending
 *			xdmcp/MaxSessions
 *			xdmcp/MaxWait
 *			xdmcp/DisplaysPerHost
 *			xdmcp/HonorIndirect
 *			xdmcp/MaxPendingIndirect
 *			xdmcp/MaxWaitIndirect
 *			xdmcp/PingIntervalSeconds (only affects new connections)
 *   		 daemon/TimedLogin (2.3.90.3)
 *   		 daemon/TimedLoginEnable (2.3.90.3)
 *   		 daemon/TimedLoginDelay (2.3.90.3)
 *   		 greeter/SystemMenu (2.3.90.3)
 *   		 greeter/ConfigAvailable (2.3.90.3)
 *   		 greeter/ChooserButton (2.4.2.0)
 *               greeter/SoundOnLoginFile (2.5.90.0)
 *               daemon/AddGtkModules (2.5.90.0)
 *               daemon/GtkModulesList (2.5.90.0)
 * Supported since: 2.3.90.2
 * Arguments:  <key>
 *   <key> is just the base part of the key such as "security/AllowRemoteRoot"
 * Answers:
 *   OK
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      50 = Unsupported key
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_GREETERPIDS  "GREETERPIDS" /* None */
/* GREETERPIDS: List all greeter pids so that one can send HUP to them
 * for config rereading.  Of course one must be root to do that.
 * Supported since: 2.3.90.2
 * Arguments:  None
 * Answers:
 *   OK <pid>;<pid>;...
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_QUERY_LOGOUT_ACTION "QUERY_LOGOUT_ACTION" /* None */
/* QUERY_LOGOUT_ACTION: Query which logout actions are possible
 * Only supported on connections that passed AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Answers:
 *   OK <action>;<action>;...
 *      Where action is one of HALT, REBOOT or SUSPEND.  An empty list
 *      can also be returned if no action is possible.  A '!' is appended
 *      to an action if it was already set with SET_LOGOUT_ACTION or
 *      SET_SAFE_LOGOUT_ACTION.  Note that SET_LOGOUT_ACTION has precedence
 *      over SET_SAFE_LOGOUT_ACTION.
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      100 = Not authenticated
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_SET_LOGOUT_ACTION "SET_LOGOUT_ACTION" /* <action> */
/* SET_LOGOUT_ACTION:  Tell the daemon to halt/reboot/suspend after slave
 * process exits. 
 * Only supported on connections that passed AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Arguments:  <action>
 *   NONE           Set exit action to 'none'
 *   HALT           Set exit action to 'halt'
 *   REBOOT         Set exit action to 'reboot'
 *   SUSPEND        Set exit action to 'suspend'
 *
 * Answers:
 *   OK
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      7 = Unknown logout action, or not available
 *      100 = Not authenticated
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_SET_SAFE_LOGOUT_ACTION "SET_SAFE_LOGOUT_ACTION" /* <action> */
/* SET_SAFE_LOGOUT_ACTION:  Tell the daemon to halt/reboot/suspend after
 * everybody logs out.  If only one person logs out, then this is obviously
 * the same as the SET_LOGOUT_ACTION.  Note that SET_LOGOUT_ACTION has
 * precendence over SET_SAFE_LOGOUT_ACTION if it is set to something other
 * then NONE.  If no one is logged in, then the action takes effect
 * immedeately.
 * Only supported on connections that passed AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Arguments:  <action>
 *   NONE           Set exit action to 'none'
 *   HALT           Set exit action to 'halt'
 *   REBOOT         Set exit action to 'reboot'
 *   SUSPEND        Set exit action to 'suspend'
 *
 * Answers:
 *   OK
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      7 = Unknown logout action, or not available
 *      100 = Not authenticated
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_LOGOUT_ACTION_NONE	"NONE"
#define GDM_SUP_LOGOUT_ACTION_HALT	"HALT"
#define GDM_SUP_LOGOUT_ACTION_REBOOT	"REBOOT"
#define GDM_SUP_LOGOUT_ACTION_SUSPEND	"SUSPEND"
/*
 */
#define GDM_SUP_QUERY_VT "QUERY_VT" /* None */
/* QUERY_VT:  Ask the daemon about which VT we are currently on.
 * This is useful for logins which don't own /dev/console but are
 * still console logins.  Only supported on Linux currently, other places
 * will just get ERROR 8.  This is also the way to query if VT
 * support is available in the daemon in the first place.
 * Only supported on connections that passed AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Arguments:  None
 * Answers:
 *   OK <vt number>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      8 = Virtual terminals not supported
 *      100 = Not authenticated
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_SET_VT "SET_VT" /* <vt> */
/* SET_VT:  Change to the specified virtual terminal.
 * This is useful for logins which don't own /dev/console but are
 * still console logins.  Only supported on Linux currently, other places
 * will just get ERROR 8.
 * Only supported on connections that passed AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Arguments:  None
 * Answers:
 *   OK
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      8 = Virtual terminals not supported
 *      9 = Invalid virtual terminal number
 *      100 = Not authenticated
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_CLOSE        "CLOSE" /* None */
/* CLOSE Answers: None
 * Supported since: 2.2.4.0
 */

#define GDM_SUP_ADD_DYNAMIC_DISPLAY	"ADD_DYNAMIC_DISPLAY" 
/*
 * ADD_X_SERVER: Add a dynamic display configuration.
 *   Configures a dynamic X server to run on the specified display
 *   leaving it in DISPLAY_CONFIG state.
 * Supported since: 2.8.0.0
 * Arguments:  <display to run on>=<server>
 *   Where <server> is either a configuration named in gdm.conf or
 *   a literal command name.
 * Answers:
 *  OK
 *  ERROR
 *     0 = Not implemented
 *     999 = Unknown error
 */
#define GDM_SUP_REMOVE_DYNAMIC_DISPLAY	"REMOVE_DYNAMIC_DISPLAY" 
/*
 * REMOVE_X_SERVER: Remove a dynamic display
 *   Removes a dynamic display, killing the server and purging
 *   the display configuration
 * Supported since: 2.8.0.0
 * Arguments:  <display to remove>
 * Answers:
 *  OK
 *  ERROR
 *     0 = Not implemented
 *     999 = Unknown error
 */
#define GDM_SUP_RELEASE_DYNAMIC_DISPLAYS	"RELEASE_DYNAMIC_DISPLAYS"
/*
 * RELEASE_SERVERS: Release dynamic displays currently in PAUSED state
 * Supported since: 2.8.0.0
 * Arguments: None
 * Answers:
 *  OK
 *  ERROR
 *     0 = Not implemented
 *     999 = Unknown error
 */

/* User flags for the SUP protocol */
enum {
	GDM_SUP_FLAG_AUTHENTICATED = 0x1, /* authenticated as a local user,
					  * from a local display we started */
	GDM_SUP_FLAG_AUTH_GLOBAL = 0x2 /* authenticated with global cookie */
};

/* Macros to check authentication level */
#define GDM_CONN_AUTHENTICATED(conn) \
	((gdm_connection_get_user_flags (conn) & GDM_SUP_FLAG_AUTHENTICATED) || \
	 (gdm_connection_get_user_flags (conn) & GDM_SUP_FLAG_AUTH_GLOBAL))

#define GDM_CONN_AUTH_GLOBAL(conn) \
	 (gdm_connection_get_user_flags (conn) & GDM_SUP_FLAG_AUTH_GLOBAL)


#define NEVER_FAILS_seteuid(uid) \
	{ int r = 0; \
	  if (geteuid () != uid) \
	    r = seteuid (uid); \
	  if G_UNLIKELY (r != 0) \
        gdm_fail ("GDM file %s: line %d (%s): Cannot run seteuid to %d: %s", \
		  __FILE__,						\
		  __LINE__,						\
		  __PRETTY_FUNCTION__,					\
                  (int)uid,						\
		  strerror (errno));			}
#define NEVER_FAILS_setegid(gid) \
	{ int r = 0; \
	  if (getegid () != gid) \
	    r = setegid (gid); \
	  if G_UNLIKELY (r != 0) \
        gdm_fail ("GDM file %s: line %d (%s): Cannot run setegid to %d: %s", \
		  __FILE__,						\
		  __LINE__,						\
		  __PRETTY_FUNCTION__,					\
                  (int)gid,						\
		  strerror (errno));			}

/* first goes to euid-root and then sets the egid and euid, to make sure
 * this succeeds */
#define NEVER_FAILS_root_set_euid_egid(uid,gid) \
	{ NEVER_FAILS_seteuid (0); \
	  NEVER_FAILS_setegid (gid); \
	  if (uid != 0) { NEVER_FAILS_seteuid (uid); } }

#endif /* GDM_H */

/* EOF */
