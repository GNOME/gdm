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


#define STX 0x2			/* Start of txt */
#define BEL 0x7			/* Bell, used to interrupt login for
				 * say timed login or something similar */

#define TYPE_LOCAL 1		/* Local X server */
#define TYPE_XDMCP 2		/* Remote display */

#define SERVER_SUCCESS 0	/* X server default */
#define SERVER_FAILURE 1	/* X server default */
#define SERVER_TIMEOUT 2	/* Server didn't start */
#define SERVER_NOTFOUND 127	/* X server default */
#define SERVER_DEAD 250		/* Server stopped */
#define SERVER_STARTED 251	/* Server started but not ready for connections yet */
#define SERVER_RUNNING 252	/* Server running and ready for connections */
#define SERVER_ABORT 253	/* Server failed badly. Suspending display. */

#define DISPLAY_SUCCESS 0	/* All systems are go */
#define DISPLAY_REMANAGE 2	/* Restart display */
#define DISPLAY_ABORT 4		/* Houston, we have a problem */
#define DISPLAY_REBOOT 8	/* Rebewt */
#define DISPLAY_HALT 16		/* Halt */
#define DISPLAY_DEAD 32		/* Display not configured/started yet */

#define XDMCP_DEAD 0
#define XDMCP_PENDING 1
#define XDMCP_MANAGED 2


/* Opcodes for the highly sophisticated protocol used for
 * daemon<->greeter communications */
#define GDM_MSGERR     'D'
#define GDM_NOECHO     'U'
#define GDM_PROMPT     'N'
#define GDM_SESS       'G'
#define GDM_LANG       '&'
#define GDM_SSESS      'C'
#define GDM_SLANG      'R'
#define GDM_RESET      'A'
#define GDM_QUIT       'P'
#define GDM_STOP       '!'
/* crap, these don't fit into the above theme, this protocol
 * is thus liable to change */
#define GDM_GNOMESESS  '?'
#define GDM_STARTTIMER 's'
#define GDM_STOPTIMER  'S'
#define GDM_LOGIN      'L' /* this is the login prompt, much like PROMPT but
			      different */
#define GDM_SETLOGIN   'l' /* this just sets the login to be this, just for
			      the greeters knowledge */
#define GDM_DISABLE    '-' /* disable the login screen */
#define GDM_ENABLE     '+' /* enable the login screen */
#define GDM_RESETOK    'r' /* reset but don't shake */

/* Different login interruptions */
#define GDM_INTERRUPT_TIMED_LOGIN 'T'
#define GDM_INTERRUPT_CONFIGURE   'C'

/* The dreaded miscellaneous category */
#define MAX_ARGS 32
#define FIELD_SIZE 256
#define PIPE_SIZE 4096

/* Configuration constants */
#define GDM_KEY_CHOOSER "daemon/Chooser=" EXPANDED_BINDIR "/gdmchooser --disable-sound --disable-crash-dialog"
/* This defaults to true for backward compatibility,
 * it will not actually do automatic login since the AutomaticLogin defaults
 * to nothing */
#define GDM_KEY_AUTOMATICLOGIN_ENABLE "daemon/AutomaticLoginEnable=true"
#define GDM_KEY_AUTOMATICLOGIN "daemon/AutomaticLogin="
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
#define GDM_KEY_REBOOT "daemon/RebootCommand=/sbin/shutdown -r now"
#define GDM_KEY_ROOTPATH "daemon/RootPath=/sbin:/usr/sbin:/bin:/usr/bin:/usr/bin/X11:/usr/local/bin:" EXPANDED_BINDIR
#define GDM_KEY_GNOMEDEFAULTSESSION "daemon/GnomeDefaultSession=" EXPANDED_DATADIR "/gnome/default.session"
#define GDM_KEY_SERVAUTH "daemon/ServAuthDir=" EXPANDED_AUTHDIR
#define GDM_KEY_SESSDIR "daemon/SessionDir=" EXPANDED_SYSCONFDIR "/gdm/Sessions/"

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

#define GDM_KEY_ALLOWROOT "security/AllowRoot=true"
#define GDM_KEY_ALLOWREMOTEROOT "security/AllowRemoteRoot=true"
#define GDM_KEY_MAXFILE "security/UserMaxFile=65536"
#define GDM_KEY_SESSIONMAXFILE "security/SessionMaxFile=524288"
#define GDM_KEY_RELAXPERM "security/RelaxPermissions=0"
#define GDM_KEY_RETRYDELAY "security/RetryDelay=3"
#define GDM_KEY_VERBAUTH "security/VerboseAuth=true"

#define GDM_KEY_XDMCP "xdmcp/Enable=false"
#define GDM_KEY_MAXPEND "xdmcp/MaxPending=4"
#define GDM_KEY_MAXSESS "xdmcp/MaxSessions=16"
#define GDM_KEY_MAXWAIT "xdmcp/MaxWait=30"
#define GDM_KEY_DISPERHOST "xdmcp/DisplaysPerHost=1"
#define GDM_KEY_UDPPORT "xdmcp/Port=177"
#define GDM_KEY_INDIRECT "xdmcp/HonorIndirect=false"
#define GDM_KEY_MAXINDIR "xdmcp/MaxPendingIndirect=4"
#define GDM_KEY_MAXINDWAIT "xdmcp/MaxWaitIndirect=30"

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
#define GDM_KEY_CONFIGURATOR "daemon/Configurator=/usr/sbin/gdmconfig"
#define GDM_KEY_CONFIG_AVAILABLE "greeter/ConfigAvailable=true"
#define GDM_KEY_TITLE_BAR "greeter/TitleBar=true"
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

#define GDM_KEY_SCAN "chooser/ScanTime=3"
#define GDM_KEY_HOST "chooser/DefaultHostImg=" EXPANDED_PIXMAPDIR "/nohost.png"
#define GDM_KEY_HOSTDIR "chooser/HostImageDir=" EXPANDED_DATADIR "/hosts/"

#define GDM_KEY_DEBUG "debug/Enable=false"

#define GDM_KEY_SERVERS "servers"


#define GDM_SESSION_FAILSAFE_GNOME "GDM_Failsafe.GNOME"
#define GDM_SESSION_FAILSAFE_XTERM "GDM_Failsafe.XTERM"
#define GDM_SESSION_GNOME_CHOOSER "Gnome Chooser"


typedef struct _GdmDisplay GdmDisplay;

struct _GdmDisplay {
    CARD32 sessionid;
    Display *dsp;
    gchar *authfile;
    GSList *auths; 
    gchar *userauth;
    gboolean authfb;
    gchar *command;
    gchar *cookie;
    gchar *bcookie;
    gchar *name;
    gchar *hostname;
    guint8 dispstat;
    guint16 dispnum;
    guint8 servstat;
    guint8 type;
    pid_t greetpid;
    pid_t servpid;
    pid_t sesspid;
    pid_t slavepid;
    pid_t chooserpid;
    time_t acctime;

    time_t last_start_time;
    gint retry_count;

    int sleep_before_run;

    gboolean disabled;

    gboolean timed_login_ok;

    int screenx;
    int screeny;
};


typedef struct _GdmIndirectDisplay GdmIndirectDisplay;

struct _GdmIndirectDisplay {
    struct sockaddr_in* dsp_sa;
    time_t acctime;
};


typedef struct _GdmChooserDisplay GdmChooserDisplay;

struct _GdmChooserDisplay {
    struct sockaddr_in* dsp_sa;
    guint16 dispnum;
    gchar *manager;
    time_t acctime;
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


void gdm_run (void);
void gdm_quit (void);

#define gdm_string_empty(x) ((x)==NULL||(x)[0]=='\0')
#define gdm_sure_string(x) ((x)!=NULL?(x):"")

#endif /* GDM_H */

/* EOF */
