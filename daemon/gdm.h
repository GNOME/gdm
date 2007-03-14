/* GDM - The GNOME Display Manager
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
#include <glib/gstdio.h>
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

#define TYPE_STATIC 1		/* X server defined in GDM configuration */
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
#define GDM_INTERRUPT_CUSTOM_CMD  'M'
#define GDM_INTERRUPT_CANCEL      'X'

/* List delimiter for config file lists */
#define GDM_DELIMITER_MODULES ":"
#define GDM_DELIMITER_THEMES "/:"

/* The dreaded miscellaneous category */
#define FIELD_SIZE 256
#define PIPE_SIZE 4096

/*
 * The following section contains keys used by the GDM configuration files.
 * The key/value pairs defined in the GDM configuration files are considered
 * "stable" interface and should only change in ways that are backwards
 * compatible.  Please keep this in mind when changing GDM configuration.
 * 
 * Developers who add new configuration options should ensure that they do the
 * following:
 * 
 * + Edit the config/gdm.conf file to include the default setting.
 *
 * + Specify the same default in this file as in the config/gdm.conf.in file.
 *
 * + Update the gdm_config_init function in daemon/gdmconfig.c to add the
 *   new key.  Include some documentation about the new key, following the
 *   style of existing comments.
 *
 * + Add any validation to the _gdm_set_value_string, _gdm_set_value_int,
 *   and/or _gdm_set_value_bool functions (depending on the type of the new
 *   key) in gdmconfig.c, if validation is needed.
 *
 * + If GDM_UPDATE_CONFIG should not respond to this configuration setting,
 *   update the update_config function in gdmconfig.c to return FALSE for
 *   this key.  Examples include changing the PidFile, ServAuthDir, or
 *   other values that GDM should not change until it is restarted.  If 
 *   this is true, the next bullet can be ignored.
 *
 * + If the option should cause the greeter (gdmlogin/gdmgreeter) program to
 *   be updated immediately, make sure to update the appropriate 
 *   _gdm_set_value_* function in gdmconfig.c.  This function calls the
 *   notify_displays_* function to call when this value is changed, so you
 *   will need to add your new config value to the list of values sending
 *   such notification.  Supporting logic will need to be added to
 *   gdm_slave_handle_notify function in slave.c to process the notify.
 *   It should be clear to see how to do this from the existing code.
 *
 * + Add the key to the gdm_read_config and gdm_reread_config functions in 
 *   gui/gdmlogin.c, gui/gdmchooser.c, and gui/greeter/greeter.c
 *   if the key is used by those programs.  Note that all GDM slaves load
 *   all their configuration data between calls to gdmcomm_comm_bulk_start()
 *   and gdmcomm_comm_bulk_stop().  This makes sure that the slave only uses
 *   a single sockets connection to get all configuration data.  If a new
 *   config value is read by a slave, make sure to load the key in this
 *   code section for best performance.
 *
 * + The gui/gdmsetup.c program should be updated to support the new option
 *   unless there's a good reason not to (like it is a configuration value
 *   that only someone who really knows what they are doing should change
 *   like GDM_KEY_PID_FILE).
 * 
 * + Currently GDM treats any key in the "gui" and "greeter" categories,
 *   and security/PamStack as available for per-display configuration.  
 *   If a key is appropriate for per-display configuration, and is not
 *   in the "gui" or "greeter" categories, then it will need to be added
 *   to the gdm_config_key_to_string_per_display function.  It may make
 *   sense for some keys used by the daemon to be per-display so this
 *   will need to be coded (refer to GDM_KEY_PAM_STACK for an example).
 *
 * + Update the docs/C/gdm.xml file to include information about the new
 *   option.  Include information about any other interfaces (such as 
 *   ENVIRONMENT variables) that may affect the configuration option.
 *   Patches without documentation will not be accepted.
 *
 * Please do this work *before* submitting an patch.  Patches that are not
 * complete will not likely be accepted.
 */

/* Configuration constants */
#define GDM_KEY_CHOOSER "daemon/Chooser=" LIBEXECDIR "/gdmchooser"
#define GDM_KEY_AUTOMATIC_LOGIN_ENABLE "daemon/AutomaticLoginEnable=false"
#define GDM_KEY_AUTOMATIC_LOGIN "daemon/AutomaticLogin="
/* The SDTLOGIN feature is Solaris specific, and causes the Xserver to be
 * run with user permissionsinstead of as root, which adds security but
 * disables the AlwaysRestartServer option as highlighted in the gdm
 * documentation */
#define GDM_KEY_ALWAYS_RESTART_SERVER "daemon/AlwaysRestartServer=" ALWAYS_RESTART_SERVER
#define GDM_KEY_GREETER "daemon/Greeter=" LIBEXECDIR "/gdmlogin"
#define GDM_KEY_REMOTE_GREETER "daemon/RemoteGreeter=" LIBEXECDIR "/gdmlogin"
#define GDM_KEY_ADD_GTK_MODULES "daemon/AddGtkModules=false"
#define GDM_KEY_GTK_MODULES_LIST "daemon/GtkModulesList="
#define GDM_KEY_GROUP "daemon/Group=gdm"
#define GDM_KEY_HALT "daemon/HaltCommand=" HALT_COMMAND
#define GDM_KEY_DISPLAY_INIT_DIR "daemon/DisplayInitDir=" GDMCONFDIR "/Init"
#define GDM_KEY_KILL_INIT_CLIENTS "daemon/KillInitClients=true"
#define GDM_KEY_LOG_DIR "daemon/LogDir=" LOGDIR
#define GDM_KEY_PATH "daemon/DefaultPath=" GDM_USER_PATH
#define GDM_KEY_PID_FILE "daemon/PidFile=/var/run/gdm.pid"
#define GDM_KEY_POSTSESSION "daemon/PostSessionScriptDir=" GDMCONFDIR "/PostSession/"
#define GDM_KEY_PRESESSION "daemon/PreSessionScriptDir=" GDMCONFDIR "/PreSession/"
#define GDM_KEY_POSTLOGIN "daemon/PostLoginScriptDir=" GDMCONFDIR "/PreSession/"
#define GDM_KEY_FAILSAFE_XSERVER "daemon/FailsafeXServer="
#define GDM_KEY_X_KEEPS_CRASHING "daemon/XKeepsCrashing=" GDMCONFDIR "/XKeepsCrashing"
#define GDM_KEY_REBOOT  "daemon/RebootCommand=" REBOOT_COMMAND
#define GDM_KEY_CUSTOM_CMD_TEMPLATE "customcommand/CustomCommand"
#define GDM_KEY_CUSTOM_CMD_LABEL_TEMPLATE "customcommand/CustomCommandLabel"
#define GDM_KEY_CUSTOM_CMD_LR_LABEL_TEMPLATE "customcommand/CustomCommandLRLabel"
#define GDM_KEY_CUSTOM_CMD_TEXT_TEMPLATE "customcommand/CustomCommandText"
#define GDM_KEY_CUSTOM_CMD_TOOLTIP_TEMPLATE "customcommand/CustomCommandTooltip"
#define GDM_KEY_CUSTOM_CMD_NO_RESTART_TEMPLATE "customcommand/CustomCommandNoRestart"
#define GDM_KEY_CUSTOM_CMD_IS_PERSISTENT_TEMPLATE "customcommand/CustomCommandIsPersistent"
#define GDM_KEY_ROOT_PATH "daemon/RootPath=/sbin:/usr/sbin:" GDM_USER_PATH
#define GDM_KEY_SERV_AUTHDIR "daemon/ServAuthDir=" AUTHDIR
#define GDM_KEY_SESSION_DESKTOP_DIR "daemon/SessionDesktopDir=/etc/X11/sessions/:" DMCONFDIR "/Sessions/:" DATADIR "/gdm/BuiltInSessions/:" DATADIR "/xsessions/"
#define GDM_KEY_BASE_XSESSION "daemon/BaseXsession=" GDMCONFDIR "/Xsession"
#define GDM_KEY_DEFAULT_SESSION "daemon/DefaultSession=gnome.desktop"
#define GDM_KEY_SUSPEND "daemon/SuspendCommand=" SUSPEND_COMMAND

#define GDM_KEY_USER_AUTHDIR "daemon/UserAuthDir="
#define GDM_KEY_USER_AUTHDIR_FALLBACK "daemon/UserAuthFBDir=/tmp"
#define GDM_KEY_USER_AUTHFILE "daemon/UserAuthFile=.Xauthority"
#define GDM_KEY_USER "daemon/User=gdm"
#define GDM_KEY_CONSOLE_NOTIFY "daemon/ConsoleNotify=true"

#define GDM_KEY_DOUBLE_LOGIN_WARNING "daemon/DoubleLoginWarning=true"
#define GDM_KEY_ALWAYS_LOGIN_CURRENT_SESSION "daemon/AlwaysLoginCurrentSession=true"

#define GDM_KEY_DISPLAY_LAST_LOGIN "daemon/DisplayLastLogin=false"

#define GDM_KEY_TIMED_LOGIN_ENABLE "daemon/TimedLoginEnable=false"
#define GDM_KEY_TIMED_LOGIN "daemon/TimedLogin="
#define GDM_KEY_TIMED_LOGIN_DELAY "daemon/TimedLoginDelay=30"

#define GDM_KEY_FLEXI_REAP_DELAY_MINUTES "daemon/FlexiReapDelayMinutes=5"

#define GDM_KEY_STANDARD_XSERVER "daemon/StandardXServer=" X_SERVER
#define GDM_KEY_FLEXIBLE_XSERVERS "daemon/FlexibleXServers=5"
#define GDM_KEY_DYNAMIC_XSERVERS "daemon/DynamicXServers=false"
#define GDM_KEY_XNEST "daemon/Xnest=" X_XNEST_CMD " " X_XNEST_CONFIG_OPTIONS
#define GDM_KEY_XNEST_UNSCALED_FONT_PATH "daemon/XnestUnscaledFontPath=" X_XNEST_UNSCALED_FONTPATH
/* Keys for automatic VT allocation rather then letting it up to the
 * X server */
#define GDM_KEY_FIRST_VT "daemon/FirstVT=7"
#define GDM_KEY_VT_ALLOCATION "daemon/VTAllocation=true"

#define GDM_KEY_CONSOLE_CANNOT_HANDLE "daemon/ConsoleCannotHandle=am,ar,az,bn,el,fa,gu,hi,ja,ko,ml,mr,pa,ta,zh"

/* How long to wait before assuming an Xserver has timed out */
#define GDM_KEY_XSERVER_TIMEOUT "daemon/GdmXserverTimeout=10"

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
/* select a nice level to run the X server at */
#define GDM_KEY_SERVER_PRIORITY "priority=0"

#define GDM_KEY_ALLOW_ROOT "security/AllowRoot=true"
#define GDM_KEY_ALLOW_REMOTE_ROOT "security/AllowRemoteRoot=false"
#define GDM_KEY_ALLOW_REMOTE_AUTOLOGIN "security/AllowRemoteAutoLogin=false"
#define GDM_KEY_USER_MAX_FILE "security/UserMaxFile=65536"
#define GDM_KEY_RELAX_PERM "security/RelaxPermissions=0"
#define GDM_KEY_CHECK_DIR_OWNER "security/CheckDirOwner=true"
#define GDM_KEY_SUPPORT_AUTOMOUNT "security/SupportAutomount=false"
#define GDM_KEY_RETRY_DELAY "security/RetryDelay=1"
#define GDM_KEY_DISALLOW_TCP "security/DisallowTCP=true"
#define GDM_KEY_PAM_STACK "security/PamStack=gdm"

#define GDM_KEY_NEVER_PLACE_COOKIES_ON_NFS "security/NeverPlaceCookiesOnNFS=true"
#define GDM_KEY_PASSWORD_REQUIRED "security/PasswordRequired=false"

#define GDM_KEY_XDMCP "xdmcp/Enable=false"
#define GDM_KEY_MAX_PENDING "xdmcp/MaxPending=4"
#define GDM_KEY_MAX_SESSIONS "xdmcp/MaxSessions=16"
#define GDM_KEY_MAX_WAIT "xdmcp/MaxWait=15"
#define GDM_KEY_DISPLAYS_PER_HOST "xdmcp/DisplaysPerHost=2"
#define GDM_KEY_UDP_PORT "xdmcp/Port=177"
#define GDM_KEY_INDIRECT "xdmcp/HonorIndirect=true"
#define GDM_KEY_MAX_INDIRECT "xdmcp/MaxPendingIndirect=4"
#define GDM_KEY_MAX_WAIT_INDIRECT "xdmcp/MaxWaitIndirect=15"
#define GDM_KEY_PING_INTERVAL "xdmcp/PingIntervalSeconds=15"
#define GDM_KEY_WILLING "xdmcp/Willing=" GDMCONFDIR "/Xwilling"

#define GDM_KEY_XDMCP_PROXY "xdmcp/EnableProxy=false"
#define GDM_KEY_XDMCP_PROXY_XSERVER "xdmcp/ProxyXServer="
#define GDM_KEY_XDMCP_PROXY_RECONNECT "xdmcp/ProxyReconnect="

#define GDM_KEY_GTK_THEME "gui/GtkTheme=Default"
#define GDM_KEY_GTKRC "gui/GtkRC=" DATADIR "/themes/Default/gtk-2.0/gtkrc"
#define GDM_KEY_MAX_ICON_WIDTH "gui/MaxIconWidth=128"
#define GDM_KEY_MAX_ICON_HEIGHT "gui/MaxIconHeight=128"

#define GDM_KEY_ALLOW_GTK_THEME_CHANGE "gui/AllowGtkThemeChange=true"
#define GDM_KEY_GTK_THEMES_TO_ALLOW "gui/GtkThemesToAllow=all"

#define GDM_KEY_BROWSER "greeter/Browser=false"
#define GDM_KEY_INCLUDE "greeter/Include="
#define GDM_KEY_EXCLUDE "greeter/Exclude=bin,daemon,adm,lp,sync,shutdown,halt,mail,news,uucp,operator,nobody,gdm,postgres,pvm,rpm,nfsnobody,pcap"
#define GDM_KEY_INCLUDE_ALL "greeter/IncludeAll=false"
#define GDM_KEY_MINIMAL_UID "greeter/MinimalUID=100"
#define GDM_KEY_DEFAULT_FACE "greeter/DefaultFace=" PIXMAPDIR "/nobody.png"
#define GDM_KEY_GLOBAL_FACE_DIR "greeter/GlobalFaceDir=" DATADIR "/pixmaps/faces/"
#define GDM_KEY_LOCALE_FILE "greeter/LocaleFile=" GDMLOCALEDIR "/locale.alias"
#define GDM_KEY_LOGO "greeter/Logo=" PIXMAPDIR "/gdm-foot-logo.png"
#define GDM_KEY_CHOOSER_BUTTON_LOGO "greeter/ChooserButtonLogo=" PIXMAPDIR "/gdm-foot-logo.png"
#define GDM_KEY_QUIVER "greeter/Quiver=true"
#define GDM_KEY_SYSTEM_MENU "greeter/SystemMenu=true"
#define GDM_KEY_CONFIGURATOR "daemon/Configurator=" SBINDIR "/gdmsetup --disable-sound --disable-crash-dialog"
#define GDM_KEY_CONFIG_AVAILABLE "greeter/ConfigAvailable=true"
#define GDM_KEY_CHOOSER_BUTTON "greeter/ChooserButton=true"
#define GDM_KEY_TITLE_BAR "greeter/TitleBar=true"

/*
 * For backwards compatibility, do not set values for DEFAULT_WELCOME or 
 * DEFAULT_REMOTEWELCOME.  This will cause these values to always be 
 * read from the config file, and will cause them to return FALSE if
 * no value is set in the config file.  We want the value "FALSE" if
 * the values don't exist in the config file.  The daemon will compare
 * the Welcome/RemoveWelcome value with the default string and 
 * automatically translate the text if the string is the same as the
 * default string.  We set the default values of GDM_KEY_WELCOME and
 * GDM_KEY_REMOTEWELCOME so that the default value is returned when
 * you run GET_CONFIG on these keys.
 */
#define GDM_DEFAULT_WELCOME_MSG "Welcome"
#define GDM_DEFAULT_REMOTE_WELCOME_MSG "Welcome to %n"

#define GDM_KEY_DEFAULT_WELCOME "greeter/DefaultWelcome=" 
#define GDM_KEY_DEFAULT_REMOTE_WELCOME "greeter/DefaultRemoteWelcome=" 
#define GDM_KEY_WELCOME "greeter/Welcome=" GDM_DEFAULT_WELCOME_MSG
#define GDM_KEY_REMOTE_WELCOME "greeter/RemoteWelcome=" GDM_DEFAULT_REMOTE_WELCOME_MSG
#define GDM_KEY_XINERAMA_SCREEN "greeter/XineramaScreen=0"
#define GDM_KEY_BACKGROUND_PROGRAM "greeter/BackgroundProgram="
#define GDM_KEY_RUN_BACKGROUND_PROGRAM_ALWAYS "greeter/RunBackgroundProgramAlways=false"
#define GDM_KEY_BACKGROUND_PROGRAM_INITIAL_DELAY "greeter/BackgroundProgramInitialDelay=30"
#define GDM_KEY_RESTART_BACKGROUND_PROGRAM "greeter/RestartBackgroundProgram=true"
#define GDM_KEY_BACKGROUND_PROGRAM_RESTART_DELAY "greeter/BackgroundProgramRestartDelay=30"
#define GDM_KEY_BACKGROUND_IMAGE "greeter/BackgroundImage="
#define GDM_KEY_BACKGROUND_COLOR "greeter/BackgroundColor=#76848F"
#define GDM_KEY_BACKGROUND_TYPE "greeter/BackgroundType=2"
#define GDM_KEY_BACKGROUND_SCALE_TO_FIT "greeter/BackgroundScaleToFit=true"
#define GDM_KEY_BACKGROUND_REMOTE_ONLY_COLOR "greeter/BackgroundRemoteOnlyColor=true"
#define GDM_KEY_LOCK_POSITION "greeter/LockPosition=false"
#define GDM_KEY_SET_POSITION "greeter/SetPosition=false"
#define GDM_KEY_POSITION_X "greeter/PositionX=0"
#define GDM_KEY_POSITION_Y "greeter/PositionY=0"
#define GDM_KEY_USE_24_CLOCK "greeter/Use24Clock=auto"
#define GDM_KEY_ENTRY_CIRCLES "greeter/UseCirclesInEntry=false"
#define GDM_KEY_ENTRY_INVISIBLE "greeter/UseInvisibleInEntry=false"
#define GDM_KEY_GRAPHICAL_THEME "greeter/GraphicalTheme=circles"
#define GDM_KEY_GRAPHICAL_THEMES "greeter/GraphicalThemes=circles/:happygnome"
#define GDM_KEY_GRAPHICAL_THEME_RAND "greeter/GraphicalThemeRand=false"
#define GDM_KEY_GRAPHICAL_THEME_DIR "greeter/GraphicalThemeDir=" DATADIR "/gdm/themes/"
#define GDM_KEY_GRAPHICAL_THEMED_COLOR "greeter/GraphicalThemedColor=#76848F"

#define GDM_KEY_INFO_MSG_FILE "greeter/InfoMsgFile="
#define GDM_KEY_INFO_MSG_FONT "greeter/InfoMsgFont="

#define GDM_KEY_PRE_FETCH_PROGRAM "greeter/PreFetchProgram="

#define GDM_KEY_SOUND_ON_LOGIN "greeter/SoundOnLogin=true"
#define GDM_KEY_SOUND_ON_LOGIN_SUCCESS "greeter/SoundOnLoginSuccess=false"
#define GDM_KEY_SOUND_ON_LOGIN_FAILURE "greeter/SoundOnLoginFailure=false"
#define GDM_KEY_SOUND_ON_LOGIN_FILE "greeter/SoundOnLoginFile="
#define GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE "greeter/SoundOnLoginSuccessFile="
#define GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE "greeter/SoundOnLoginFailureFile="
#define GDM_KEY_SOUND_PROGRAM "daemon/SoundProgram=" SOUND_PROGRAM

#define GDM_KEY_SCAN_TIME "chooser/ScanTime=4"
#define GDM_KEY_DEFAULT_HOST_IMG "chooser/DefaultHostImg=" PIXMAPDIR "/nohost.png"
#define GDM_KEY_HOST_IMAGE_DIR "chooser/HostImageDir=" DATADIR "/hosts/"
#define GDM_KEY_HOSTS "chooser/Hosts="
#define GDM_KEY_MULTICAST "chooser/Multicast=false"
#define GDM_KEY_MULTICAST_ADDR "chooser/MulticastAddr=ff02::1"
#define GDM_KEY_BROADCAST "chooser/Broadcast=true"
#define GDM_KEY_ALLOW_ADD "chooser/AllowAdd=true"

#define GDM_KEY_DEBUG "debug/Enable=false"
#define GDM_KEY_DEBUG_GESTURES "debug/Gestures=false"

#define GDM_KEY_SECTION_GREETER "greeter"
#define GDM_KEY_SECTION_SERVERS "servers"

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

#define GDM_RESPONSE_CANCEL "GDM_RESPONSE_CANCEL"

#ifndef TYPEDEF_GDM_CONNECTION
#define TYPEDEF_GDM_CONNECTION
typedef struct _GdmConnection GdmConnection;
#endif  /* TYPEDEF_GDM_CONNECTION */

#ifndef TYPEDEF_GDM_CUSTOM_CMD
#define TYPEDEF_GDM_CUSTOM_CMD
typedef struct _GdmCustomCmd GdmCustomCmd;
#endif /* TYPEDEF_GDM_CUSTOM_CMD */
struct _GdmCustomCmd {
    gchar *command; /* command(s) to execute */
    gchar *command_label; /* button/menu item label */
    gchar *command_lr_label; /* radio button/list item label */
    gchar *command_text; /* warning dialog text */
    gchar *command_tooltip; /* tooltip string */
    gboolean command_no_restart; /* no restart flag */
    gboolean command_is_persistent; /* persistence flag */
};

#define GDM_CUSTOM_COMMAND_MAX 10 /* maximum number of supported custom commands */

/* Values between GDM_LOGOUT_ACTION_CUSTOM_CMD_FIRST and 
   GDM_LOGOUT_ACTION_CUSTOM_CMD_LAST are reserved and should not be used */
typedef enum {
	GDM_LOGOUT_ACTION_NONE = 0,
	GDM_LOGOUT_ACTION_HALT,
	GDM_LOGOUT_ACTION_REBOOT,
	GDM_LOGOUT_ACTION_SUSPEND,
	GDM_LOGOUT_ACTION_CUSTOM_CMD_FIRST,
	GDM_LOGOUT_ACTION_CUSTOM_CMD_LAST = GDM_LOGOUT_ACTION_CUSTOM_CMD_FIRST + GDM_CUSTOM_COMMAND_MAX - 1,
	GDM_LOGOUT_ACTION_LAST
} GdmLogoutAction;

#ifndef TYPEDEF_GDM_DISPLAY
#define TYPEDEF_GDM_DISPLAY
typedef struct _GdmDisplay GdmDisplay;
#endif /* TYPEDEF_GDM_DISPLAY */

/* Use this to get the right authfile name */
#define GDM_AUTHFILE(display) \
	(display->authfile_gdm != NULL ? display->authfile_gdm : display->authfile)

#ifdef sun
#define SDTLOGIN_DIR "/var/dt/sdtlogin"
#endif

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

    int priority;
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

    char *preset_user;

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

typedef struct _GdmXserver GdmXserver;
struct _GdmXserver {
	char *id;
	char *name;
	char *command;
	gboolean flexible;
	gboolean choosable; /* not implemented yet */
	gboolean chooser; /* instead of greeter, run chooser */
	gboolean handled;
	int priority;
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

/*Execute custom cmd*/
#define GDM_SOP_CUSTOM_CMD "CUSTOM_CMD"  /* <slave pid> <cmd id> */

/* Start a new standard X flexible server */
#define GDM_SOP_FLEXI_XSERVER "FLEXI_XSERVER" /* no arguments */

#define GDM_SOP_SHOW_ERROR_DIALOG "SHOW_ERROR_DIALOG"  /* show the error dialog from daemon */
#define GDM_SOP_SHOW_YESNO_DIALOG "SHOW_YESNO_DIALOG"  /* show the yesno dialog from daemon */
#define GDM_SOP_SHOW_QUESTION_DIALOG "SHOW_QUESTION_DIALOG"  /* show the question dialog from daemon */
#define GDM_SOP_SHOW_ASKBUTTONS_DIALOG "SHOW_ASKBUTTON_DIALOG"  /* show the askbutton dialog from daemon */

/* Notification protocol */
/* keys */
#define GDM_NOTIFY_ALLOW_REMOTE_ROOT "AllowRemoteRoot" /* <true/false as int> */
#define GDM_NOTIFY_ALLOW_ROOT "AllowRoot" /* <true/false as int> */
#define GDM_NOTIFY_ALLOW_REMOTE_AUTOLOGIN "AllowRemoteAutoLogin" /* <true/false as int> */
#define GDM_NOTIFY_SYSTEM_MENU "SystemMenu" /* <true/false as int> */
#define GDM_NOTIFY_CONFIG_AVAILABLE "ConfigAvailable" /* <true/false as int> */
#define GDM_NOTIFY_CHOOSER_BUTTON "ChooserButton" /* <true/false as int> */
#define GDM_NOTIFY_RETRY_DELAY "RetryDelay" /* <seconds> */
#define GDM_NOTIFY_GREETER "Greeter" /* <greeter binary> */
#define GDM_NOTIFY_REMOTE_GREETER "RemoteGreeter" /* <greeter binary> */
#define GDM_NOTIFY_TIMED_LOGIN "TimedLogin" /* <login> */
#define GDM_NOTIFY_TIMED_LOGIN_DELAY "TimedLoginDelay" /* <seconds> */
#define GDM_NOTIFY_TIMED_LOGIN_ENABLE "TimedLoginEnable" /* <true/false as int> */
#define GDM_NOTIFY_DISALLOW_TCP "DisallowTCP" /* <true/false as int> */
#define GDM_NOTIFY_SOUND_ON_LOGIN_FILE "SoundOnLoginFile" /* <sound file> */
#define GDM_NOTIFY_SOUND_ON_LOGIN_SUCCESS_FILE "SoundOnLoginSuccessFile" /* <sound file> */
#define GDM_NOTIFY_SOUND_ON_LOGIN_FAILURE_FILE "SoundOnLoginFailureFile" /* <sound file> */
#define GDM_NOTIFY_ADD_GTK_MODULES "AddGtkModules" /* <true/false as int> */
#define GDM_NOTIFY_GTK_MODULES_LIST "GtkModulesList" /* <modules list> */
#define GDM_NOTIFY_CUSTOM_CMD_TEMPLATE "CustomCommand" /* <custom command path> */

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
/* send the response */
#define GDM_SLAVE_NOTIFY_RESPONSE 'R'
/* send the error dialog response */
#define GDM_SLAVE_NOTIFY_ERROR_RESPONSE 'E'
/* send the yesno dialog response */
#define GDM_SLAVE_NOTIFY_YESNO_RESPONSE 'Y'
/* send the askbuttons dialog response */
#define GDM_SLAVE_NOTIFY_ASKBUTTONS_RESPONSE 'B'
/* send the question dialog response */
#define GDM_SLAVE_NOTIFY_QUESTION_RESPONSE 'Q'

/*
 * Maximum number of messages allowed over the sockets protocol.  This
 * is set to 80 since the gdmlogin/gdmgreeter programs have ~60 config
 * values that are pulled over the socket connection so it allows them
 * all to be grabbed in one pull.
 */
#define GDM_SUP_MAX_MESSAGES 80
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
/* VERSION: Query GDM version
 * Supported since: 2.2.4.0
 * Arguments: None
 * Answers:
 *   GDM <gdm version>
 *   ERROR <err number> <english error description>
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_AUTH_LOCAL "AUTH_LOCAL" /* <xauth cookie> */
/* AUTH_LOCAL: Setup this connection as authenticated for FLEXI_SERVER.
 *             Because all full blown (non-Xnest) displays can be started
 *             only from users logged in locally, and here gdm assumes
 *             only users logged in from gdm.  They must pass the xauth
 *             MIT-MAGIC-COOKIE-1 that they were passed before the
 *             connection is authenticated.
 * Note:       The AUTH LOCAL command requires the --authenticate option,
 *             although only FLEXI XSERVER uses this currently.
 * Note:       Since 2.6.0.6 you can also use a global
 *             <ServAuthDir>/.cookie, which works for all authentication
 *             except for SET_LOGOUT_ACTION and QUERY_LOGOUT_ACTION
 *             and SET_SAFE_LOGOUT_ACTION which require a logged in
 *             display.
 * Supported since: 2.2.4.0
 * Arguments: <xauth cookie>
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
/* FLEXI_XSERVER: Start a new X flexible display.  Only supported on
 *                connection that passed AUTH_LOCAL
 * Supported since: 2.2.4.0
 * Arguments: <xserver type>
 *   If no arguments, starts the standard X server
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
#define GDM_SUP_FLEXI_XSERVER_USER  "FLEXI_XSERVER_USER" /* <username> <xserver type> */
/* FLEXI_XSERVER_USER: Start a new X flexible display and initialize the 
 *                greeter with the given username. Only supported on 
 *                connection that passed AUTH_LOCAL
 * Supported since: 2.17.7
 * Arguments: <username> <xserver type>
 *   If no server type specified, starts the standard X server
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
/* FLEXI_XNEXT: Start a new flexible Xnest display.
 * Note:        Supported on older versions from 2.2.4.0, later
 *              2.2.4.2, but since 2.3.90.4 you must supply 4 
 *              arguments or ERROR 100 will be returned.  This
 *              will start Xnest using the XAUTHORITY file
 *              supplied and as the uid same as the owner of
 *              that file (and same as you supply).  You must
 *              also supply the cookie as the third argument
 *              for this display, to prove that you indeed are
 *              this user.  Also this file must be readable
 *              ONLY by this user, that is have a mode of 0600.
 *              If this all is not met, ERROR 100 is returned.
 * Note:        The cookie should be the MIT-MAGIC-COOKIE-1,
 *              the first one gdm can find in the XAUTHORITY
 *              file for this display.  If that's not what you
 *              use you should generate one first.  The cookie
 *              should be in hex form.
 * Supported since: 2.3.90.4
 * Arguments: <display to run on> <uid of requesting user>
 *            <xauth cookie for the display> <xauth file>
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
#define GDM_SUP_FLEXI_XNEST_USER  "FLEXI_XNEST_USER" /* <username> <display> <uid> <xauth cookie> <xauth file> */
/* FLEXI_XNEXT_USER: Start a new flexible Xnest display and 
 *              initialize the greeter with the given username
 * Note:        The cookie should be the MIT-MAGIC-COOKIE-1,
 *              the first one gdm can find in the XAUTHORITY
 *              file for this display.  If that's not what you
 *              use you should generate one first.  The cookie
 *              should be in hex form.
 * Supported since: 2.17.7
 * Arguments: <username> <display to run on> <uid of requesting user>
 *            <xauth cookie for the display> <xauth file>
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
#define GDM_SUP_ADD_DYNAMIC_DISPLAY	"ADD_DYNAMIC_DISPLAY" 
/*
 * ADD_DYNAMIC_DISPLAY: Create a new server definition that will
 *                      run on the specified display leaving it
 *                      in DISPLAY_CONFIG state.
 * Supported since: 2.8.0.0
 * Arguments: <display to run on>=<server>
 *   Where <server> is either a configuration named in the
 *   GDM configuration a literal command name.
 * Answers:
 *  OK
 *  ERROR
 *     0 = Not implemented
 *     2 = Existing display
 *     3 = No server string
 *     4 = Display startup failure
 *     100 = Not authenticated
 *     200 - Dynamic Displays not allowed
 *     999 = Unknown error
 */
#define GDM_SUP_RELEASE_DYNAMIC_DISPLAYS	"RELEASE_DYNAMIC_DISPLAYS"
/*
 * RELEASE_DYNAMIC_DISPLAYS: Release and begin managing dynamic displays
 *                           currently in DISPLAY_CONFIG state
 * Supported since: 2.8.0.0
 * Arguments: <display to release>
 * Answers:
 *  OK
 *  ERROR
 *     0 = Not implemented
 *     1 = Bad display number
 *     100 = Not authenticated
 *     200 = Dynamic Displays not allowed
 *     999 = Unknown error
 */
#define GDM_SUP_REMOVE_DYNAMIC_DISPLAY	"REMOVE_DYNAMIC_DISPLAY" 
/*
 * REMOVE_DYNAMIC_DISPLAY: Remove a dynamic display, killing the server
 *                         and purging the display configuration
 * Supported since: 2.8.0.0
 * Arguments: <display to remove>
 * Answers:
 *  OK
 *  ERROR
 *     0 = Not implemented
 *     1 = Bad display number
 *     100 = Not authenticated
 *     200 = Dynamic Displays not allowed
 *     999 = Unknown error
 */
#define GDM_SUP_ATTACHED_SERVERS "ATTACHED_SERVERS" /* None */
#define GDM_SUP_CONSOLE_SERVERS  "CONSOLE_SERVERS"  /* None */
/* ATTACHED_SERVERS: List all attached displays.  Doesn't list XDMCP
 *                   and xnest non-attached displays.
 * Note:             This command used to be named CONSOLE_SERVERS,
 *                   which is still recognized for backwards
 *                   compatibility.  The optional pattern argument
 *                   is supported as of version 2.8.0.0.
 * Supported since: 2.2.4.0
 * Arguments: <pattern> (optional)
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
 *      0 = Not implemented
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_ALL_SERVERS  "ALL_SERVERS" /* None */
/* ALL_SERVERS: List all displays, including attached, remote, xnest.
 *              This can for example be useful to figure out if
 *              the display you are on is managed by the gdm daemon,
 *              by seeing if it is in the list.  It is also somewhat
 *              like the 'w' command but for graphical sessions.
 * Supported since: 2.4.2.96
 * Arguments: None
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
#define GDM_SUP_GET_SERVER_LIST "GET_SERVER_LIST" /* None */
/* GET_SERVER_LIST:  Get a list of the server sections from
 *                   the configuration file. 
 * Supported since: 2.13.0.4
 * Arguments: None 
 * Answers:
 *   OK <value>;<value>;...
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      1 = No servers found
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_GET_SERVER_DETAILS "GET_SERVER_DETAILS" /* <server> <key> */
/* GET_SERVER_DETAILS:  Get detail information for a specific server.
 * Supported since: 2.13.0.4
 * Arguments: <server> <key>
 *   Key values include:
 *     ID        - Returns the server id
 *     NAME      - Returns the server name
 *     COMMAND   - Returns the server command
 *     FLEXIBLE  - Returns "true" if flexible, "false" otherwise
 *     CHOOSABLE - Returns "true" if choosable, "false" otherwise
 *     HANDLED   - Returns "true" if handled, "false" otherwise
 *     CHOOSER   - Returns "true" if chooser, "false" otherwise
 *     PRIORITY  - Returns process priority
 * Answers: 
 *   OK <value>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      1 = Server not found
 *      2 = Key not valid
 *      50 = Unsupported key
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_GET_CONFIG "GET_CONFIG" /* <key> */
/* GET_CONFIG:  Get configuration value for key.  Useful so
 *              that other applications can request configuration
 *              information from GDM.  Any key defined as GDM_KEY_*
 *              in gdm.h is supported.  Starting with version 2.13.0.2
 *              translated keys (such as "greeter/GdmWelcome[cs]" are
 *              supported via GET_CONFIG.  Also starting with version
 *              2.13.0.2 it is no longer necessary to include the
 *              default value (i.e. you can use key "greeter/IncludeAll"
 *              instead of having to use "greeter/IncludeAll=false".
 *              Starting with version 2.14.4, GDM supports per-display
 *              configuration (for GUI slave clients only) and accepts
 *              the display argument to retrieve per-display value (if
 *              any.
 * Supported since: 2.6.0.9
 * Arguments: <key> [<display>]
 * Answers:
 *   OK <value>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      50 = Unsupported key
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_GET_CONFIG_FILE  "GET_CONFIG_FILE" /* None */
/* GET_CONFIG_FILE: Get defaults config file location being used by
 #                  the daemon.   If the GDM daemon was started
 *                  with the --config option, it will return
 *                  the value passed in via that argument.
 * Supported since: 2.8.0.2
 * Arguments: None
 * Answers:
 *   OK <full path to GDM configuration file>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_GET_CUSTOM_CONFIG_FILE  "GET_CUSTOM_CONFIG_FILE" /* None */
/* GET_CONFIG_FILE: Get custom config file location being used by
 #                  the daemon. 
 * Supported since: 2.14.0.0
 * Arguments: None
 * Answers:
 *   OK <full path to GDM custom configuration file>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      1 = File not found
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_UPDATE_CONFIG "UPDATE_CONFIG" /* <key> */
/* UPDATE_CONFIG: Tell the daemon to re-read a key from the
 *                GDM configuration file.   Any user can request
 *                that values are re-read but the daemon will
 *                only do so if the file has been modified
 *                since GDM first read the file.  Only users
 *                who can change the GDM configuration file
 *                (normally writable only by the root user) can
 *                actually modify the GDM configuration.  This
 *                command is useful to cause the GDM to update
 *                itself to recognize a change made to the GDM
 *                configuration file by the root user.
 *
 *                Starting with version 2.13.0.0, all GDM keys are
 *                supported except for the following:
 *
 *                    daemon/PidFile
 *                    daemon/ConsoleNotify
 *                    daemon/User
 *                    daemon/Group
 *                    daemon/LogDir
 *                    daemon/ServAuthDir
 *                    daemon/UserAuthDir
 *                    daemon/UserAuthFile
 *                    daemon/UserAuthFBDir
 *
 *                GDM also supports the following Psuedokeys:
 *
 *                xdmcp/PARAMETERS (2.3.90.2) updates the following:
 *                    xdmcp/MaxPending
 *                    xdmcp/MaxSessions
 *                    xdmcp/MaxWait
 *                    xdmcp/DisplaysPerHost
 *                    xdmcp/HonorIndirect
 *                    xdmcp/MaxPendingIndirect
 *                    xdmcp/MaxWaitIndirect
 *                    xdmcp/PingIntervalSeconds (only affects new connections)
 *
 *                 xservers/PARAMETERS (2.13.0.4) updates the following:
 *                    all [server-foo] sections.
 *
 *                 Supported keys for previous versions of GDM:
 *
 *                    security/AllowRoot (2.3.90.2)
 *                    security/AllowRemoteRoot (2.3.90.2)
 *                    security/AllowRemoteAutoLogin (2.3.90.2)
 *                    security/RetryDelay (2.3.90.2)
 *                    security/DisallowTCP (2.4.2.0)
 *                    daemon/Greeter (2.3.90.2)
 *                    daemon/RemoteGreeter (2.3.90.2)
 *                    xdmcp/Enable (2.3.90.2)
 *                    xdmcp/Port (2.3.90.2)
 *                    daemon/TimedLogin (2.3.90.3)
 *                    daemon/TimedLoginEnable (2.3.90.3)
 *                    daemon/TimedLoginDelay (2.3.90.3)
 *                    greeter/SystemMenu (2.3.90.3)
 *                    greeter/ConfigAvailable (2.3.90.3)
 *                    greeter/ChooserButton (2.4.2.0)
 *                    greeter/SoundOnLoginFile (2.5.90.0)
 *                    daemon/AddGtkModules (2.5.90.0)
 *                    daemon/GtkModulesList (2.5.90.0)
 * Supported since: 2.3.90.2
 * Arguments: <key>
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
/* GREETERPIDS: List all greeter pids so that one can send HUP
 *              to them for config rereading.  Of course one
 *              must be root to do that.
 * Supported since: 2.3.90.2
 * Arguments: None
 * Answers:
 *   OK <pid>;<pid>;...
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_QUERY_LOGOUT_ACTION "QUERY_LOGOUT_ACTION" /* None */
/* QUERY_LOGOUT_ACTION: Query which logout actions are possible
 *                      Only supported on connections that passed
 *                      AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Answers:
 *   OK <action>;<action>;...
 *      Where action is one of HALT, REBOOT, SUSPEND or CUSTOM_CMDX (where X in [0,GDM_CUSTOM_COMMAND_MAX)).
 *      An empty list can also be returned if no action is possible.  A '!' is appended
 *      to an action if it was already set with SET_LOGOUT_ACTION or
 *      SET_SAFE_LOGOUT_ACTION.  Note that SET_LOGOUT_ACTION has precedence
 *      over SET_SAFE_LOGOUT_ACTION.
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      100 = Not authenticated
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_QUERY_CUSTOM_CMD_LABELS "QUERY_CUSTOM_CMD_LABELS" /* None */
/* QUERY_CUSTOM_CMD_LABELS: Query labels belonging to exported custom commands
 *                          Only supported on connections that passed
 *                          AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Answers:
 *   OK <label1>;<label2>;...
 *      Where labelX is one of the labels belonging to CUSTOM_CMDX (where X in [0,GDM_CUSTOM_COMMAND_MAX)).
 *      An empty list can also be returned if none of the custom commands are 
 *      exported outside login manager (no CustomCommandIsPersistent options are set to true).  
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      100 = Not authenticated
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_QUERY_CUSTOM_CMD_NO_RESTART_STATUS "QUERY_CUSTOM_CMD_NO_RESTART_STATUS" /* None */
/* QUERY_CUSTOM_CMD_NO_RESTART_STATUS: Query NoRestart config options for each of custom commands
 *                                     Only supported on connections that passed
 *                                     AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Answers:
 *   OK <status>
 *      Where each bit of the status represents NoRestart value for each of the custom commands.
 *      bit on (1):  NoRestart = true, 
 *      bit off (0): NoRestart = false.
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      100 = Not authenticated
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_SET_LOGOUT_ACTION "SET_LOGOUT_ACTION" /* <action> */
/* SET_LOGOUT_ACTION: Tell the daemon to halt/restart/suspend after
 *                    slave process exits.  Only supported on
 *                    connections that passed AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Arguments: <action>
 *   NONE           Set exit action to 'none'
 *   HALT           Set exit action to 'halt'
 *   REBOOT         Set exit action to 'reboot'
 *   SUSPEND        Set exit action to 'suspend'
 *   CUSTOM_CMDX    Set exit action to 'custom command X' where where X in [0,GDM_CUSTOM_COMMAND_MAX)
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
/* SET_SAFE_LOGOUT_ACTION: Tell the daemon to halt/restart/suspend
 *                         after everybody logs out.  If only one
 *                         person logs out, then this is obviously
 *                         the same as the SET_LOGOUT_ACTION.  Note
 *                         that SET_LOGOUT_ACTION has precendence
 *                         over SET_SAFE_LOGOUT_ACTION if it is set
 *                         to something other then NONE.  If no one
 *                         is logged in, then the action takes effect
 *                         immedeately.  Only supported on connections
 *                         that passed AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Arguments: <action>
 *   NONE           Set exit action to 'none'
 *   HALT           Set exit action to 'halt'
 *   REBOOT         Set exit action to 'reboot'
 *   SUSPEND        Set exit action to 'suspend'
 *   CUSTOM_CMDX    Set exit action to 'custom command X' where where X in [0,GDM_CUSTOM_COMMAND_MAX)
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
#define GDM_SUP_LOGOUT_ACTION_NONE	          "NONE"
#define GDM_SUP_LOGOUT_ACTION_HALT	          "HALT"
#define GDM_SUP_LOGOUT_ACTION_REBOOT	          "REBOOT"
#define GDM_SUP_LOGOUT_ACTION_SUSPEND	          "SUSPEND"
#define GDM_SUP_LOGOUT_ACTION_CUSTOM_CMD_TEMPLATE "CUSTOM_CMD"
/*
 */
#define GDM_SUP_QUERY_VT "QUERY_VT" /* None */
/* QUERY_VT:  Ask the daemon about which VT we are currently on.
 *            This is useful for logins which don't own
 *            /dev/console but are still console logins.  Only
 *            supported on Linux currently, other places will
 *            just get ERROR 8.  This is also the way to query
 *            if VT support is available in the daemon in the
 *            first place.  Only supported on connections that
 *            passed AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Arguments: None
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
 *          This is useful for logins which don't own /dev/console
 *          but are still console logins.  Only supported on Linux
 *          currently, other places will just get ERROR 8.
 *          Only supported on connections that passed AUTH_LOCAL.
 * Supported since: 2.5.90.0
 * Arguments: None
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
#define GDM_SUP_SERVER_BUSY "SERVER_BUSY" /* None */
/* SERVER_BUSY:  Returns true if half or more of the daemon's sockets are
 *               busy, false otherwise.  Used by slave programs which want
 *               to ensure they do not overhwelm the server.
 * Supported since: 2.13.0.8
 * Arguments: None 
 * Answers:
 *   OK <value>
 *   ERROR <err number> <english error description>
 *      0 = Not implemented
 *      200 = Too many messages
 *      999 = Unknown error
 */
#define GDM_SUP_GET_SERVER_DETAILS "GET_SERVER_DETAILS" /* <server> <key> */
#define GDM_SUP_CLOSE        "CLOSE" /* None */
/* CLOSE: Close sockets connection
 * Supported since: 2.2.4.0
 * Arguments: None
 * Answers: None
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
