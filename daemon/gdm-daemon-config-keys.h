/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GDM_DAEMON_CONFIG_KEYS_H
#define _GDM_DAEMON_CONFIG_KEYS_H

#include <glib.h>

#include "gdm-config.h"

G_BEGIN_DECLS

/* BEGIN LEGACY KEYS */
#define GDM_KEY_CHOOSER "daemon/Chooser=" LIBEXECDIR "/gdmchooser"
#define GDM_KEY_AUTOMATIC_LOGIN_ENABLE "daemon/AutomaticLoginEnable=false"
#define GDM_KEY_AUTOMATIC_LOGIN "daemon/AutomaticLogin="
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
#define GDM_KEY_FIRST_VT "daemon/FirstVT=7"
#define GDM_KEY_VT_ALLOCATION "daemon/VTAllocation=true"
#define GDM_KEY_CONSOLE_CANNOT_HANDLE "daemon/ConsoleCannotHandle=am,ar,az,bn,el,fa,gu,hi,ja,ko,ml,mr,pa,ta,zh"
#define GDM_KEY_XSERVER_TIMEOUT "daemon/GdmXserverTimeout=10"

#define GDM_KEY_SERVER_PREFIX "server-"
#define GDM_KEY_SERVER_NAME "name=Standard server"
#define GDM_KEY_SERVER_COMMAND "command=" X_SERVER
#define GDM_KEY_SERVER_FLEXIBLE "flexible=true"
#define GDM_KEY_SERVER_CHOOSABLE "choosable=false"
#define GDM_KEY_SERVER_HANDLED "handled=true"
#define GDM_KEY_SERVER_CHOOSER "chooser=false"
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
/* END LEGACY KEYS */

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

G_END_DECLS

#endif /* _GDM_DAEMON_CONFIG_KEYS_H */
