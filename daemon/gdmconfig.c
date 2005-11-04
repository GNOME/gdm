/* GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 * Copyright (C) 2005 Brian Cameron <brian.cameron@sun.com>
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

/* 
 * gdmconfig.c isolates most logic that interacts with vicious-extensions
 * into a single file and provides a mechanism for interacting with GDM
 * configuration optins via access functions for getting/setting values.
 * This logic also ensures that the same configuration validation happens
 * when loading the values initially or setting them via the
 * GDM_UPDATE_CONFIG socket command.
 * 
 * When adding a new configuration option, simply add the new option to 
 * gdm.h and to the val_hash and type_hash hashes in the gdm_config_init
 * function.  Any validation for the configuration option should be
 * placed in the _gdm_set_value_string, _gdm_set_value_int, or
 * _gdm_set_value_bool functions.
 */
#include <config.h>

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>

#include <glib-2.0/glib.h>
#include <glib/gi18n.h>

#include "vicious.h"

#include "gdm.h"
#include "verify.h"
#include "gdm-net.h"
#include "misc.h"
#include "server.h"
#include "filecheck.h"

gchar *config_file = NULL;
static time_t config_file_mtime = 0;

extern gboolean no_console;
extern gboolean gdm_emergency_server;

GSList *displays = NULL;
GSList *xservers = NULL;

gint high_display_num = 0;

typedef enum {
	CONFIG_BOOL,
	CONFIG_INT,
	CONFIG_STRING
} GdmConfigType;

static GHashTable    *type_hash   = NULL;
static GHashTable    *val_hash    = NULL;
static GdmConfigType  bool_type   = CONFIG_BOOL;
static GdmConfigType  int_type    = CONFIG_INT;
static GdmConfigType  string_type = CONFIG_STRING;

static uid_t GdmUserId;   /* Userid  under which gdm should run */
static gid_t GdmGroupId;  /* Gruopid under which gdm should run */

/* Config options used by daemon */
/* ----------------------------- */
static gchar *GdmUser = NULL;
static gchar *GdmGroup = NULL;
static gchar *GdmGtkRC = NULL;
static gchar *GdmGtkTheme = NULL;
static gchar *GdmSessDir = NULL;
static gchar *GdmBaseXsession = NULL;
static gchar *GdmDefaultSession = NULL;
static gchar *GdmAutomaticLogin = NULL;
static gchar *GdmConfigurator = NULL;
static gchar *GdmGlobalFaceDir = NULL;
static gchar *GdmGreeter = NULL;
static gchar *GdmRemoteGreeter = NULL;
static gchar *GdmGtkModulesList = NULL;
static gchar *GdmChooser = NULL;
static gchar *GdmLogDir = NULL;
static gchar *GdmDisplayInit = NULL;
static gchar *GdmPostLogin = NULL;
static gchar *GdmPreSession = NULL;
static gchar *GdmPostSession = NULL;
static gchar *GdmFailsafeXserver = NULL;
static gchar *GdmXKeepsCrashing = NULL;
static gchar *GdmHalt = NULL;
static gchar *GdmHaltReal = NULL;
static gchar *GdmReboot = NULL;
static gchar *GdmRebootReal = NULL;
static gchar *GdmSuspend = NULL;
static gchar *GdmSuspendReal = NULL;
static gchar *GdmServAuthDir = NULL;
static gchar *GdmMulticastAddr;
static gchar *GdmUserAuthDir = NULL;
static gchar *GdmUserAuthFile = NULL;
static gchar *GdmUserAuthFallback = NULL;
static gchar *GdmPidFile = NULL;
static gchar *GdmPath = NULL;
static gchar *GdmRootPath = NULL;
static gchar *GdmWilling = NULL;
static gchar *GdmXdmcpProxyXserver = NULL;
static gchar *GdmXdmcpProxyReconnect = NULL;
static gchar *GdmTimedLogin = NULL;
static gchar *GdmStandardXserver = NULL;
static gchar *GdmXnest = NULL;
static gchar *GdmSoundProgram = NULL;
static gchar *GdmSoundOnLoginReadyFile = NULL;
static gchar *GdmSoundOnLoginSuccessFile = NULL;
static gchar *GdmSoundOnLoginFailureFile = NULL;
static gchar *GdmConsoleCannotHandle = NULL;

static gint GdmXineramaScreen = 0;
static gint GdmUserMaxFile = 0;
static gint GdmDisplaysPerHost = 0;
static gint GdmMaxPending = 0;
static gint GdmMaxSessions = 0;
static gint GdmUdpPort = 0;
static gint GdmMaxIndirect = 0;
static gint GdmMaxWaitIndirect = 0;
static gint GdmPingInterval = 0;
static gint GdmRelaxPerm = 0;
static gint GdmRetryDelay = 0;
static gint GdmTimedLoginDelay = 0;
static gint GdmFlexibleXservers = 5;
static gint GdmFirstVt = 7;

/* The SDTLOGIN feature is Solaris specific, and causes the Xserver to be
 * run with user permissionsinstead of as root, which adds security but
 * disables the AlwaysRestartServer option as highlighted in the gdm
 * documentation */
#ifdef sun
gboolean GdmAlwaysRestartServer = TRUE;
#else
gboolean GdmAlwaysRestartServer = FALSE;
#endif
static gboolean GdmAutomaticLoginEnable = FALSE;
static gboolean GdmConfigAvailable = FALSE;
static gboolean GdmSystemMenu = FALSE;
static gboolean GdmChooserButton = FALSE;
static gboolean GdmBrowser = FALSE;
static gboolean GdmAddGtkModules = FALSE;
static gboolean GdmDoubleLoginWarning = TRUE;
static gboolean GdmAlwaysLoginCurrentSession = FALSE;
static gboolean GdmDisplayLastLogin = TRUE;
static gboolean GdmMulticast;
static gboolean GdmNeverPlaceCookiesOnNfs = TRUE;
static gboolean GdmPasswordRequired = FALSE;
static gboolean GdmKillInitClients = FALSE;
static gboolean GdmXdmcp = FALSE;
static gboolean GdmIndirect = FALSE;
static gboolean GdmXdmcpProxy = FALSE;
static gboolean GdmDebug = FALSE;
static gboolean GdmDebugGestures = FALSE;
static gboolean GdmAllowRoot = FALSE;
static gboolean GdmAllowRemoteRoot = FALSE;
static gboolean GdmAllowRemoteAutoLogin = FALSE;
static gboolean GdmCheckDirOwner = TRUE;
static gboolean GdmTimedLoginEnable = FALSE;
static gboolean GdmDynamicXservers = FALSE;
static gboolean GdmVTAllocation = TRUE;
static gboolean GdmDisallowTcp = TRUE;
static gboolean GdmSoundOnLoginSuccess = FALSE;
static gboolean GdmSoundOnLoginFailure = FALSE;
static gboolean GdmConsoleNotify = TRUE;

/* Config options used by slave */
/* ---------------------------- */
static gchar *GdmInitDir;
static gchar *GdmFaceDir;
static gchar *GdmGtkRc;
static gchar *GdmGtkThemesToAllow;
static gchar *GdmInclude;
static gchar *GdmExclude;
static gchar *GdmFace;
static gchar *GdmLocaleFile;
static gchar *GdmLogo;
static gchar *GdmChooserButtonLogo;
static gchar *GdmWelcome;
static gchar *GdmRemoteWelcome;
static gchar *GdmBackgroundProgram;
static gchar *GdmBackgroundImage;
static gchar *GdmBackgroundColor;
static gchar *GdmGraphicalTheme;
static gchar *GdmInfoMsgFile;
static gchar *GdmInfoMsgFont;
static gchar *GdmHost;
static gchar *GdmHostImageDir;
static gchar *GdmHosts;
static gchar *GdmGraphicalThemeColor;
static gchar *GdmGraphicalThemeDir;
static gchar *GdmGraphicalThemes;

static gint GdmPositionX;
static gint GdmPositionY;
static gint GdmMinimalUid;
static gint GdmIconWidth;
static gint GdmIconHeight;
static gint GdmBackgroundType;
static gint GdmScanTime;
static gint GdmMaxWait;

static gboolean GdmAllowGtkThemeChange;
static gboolean GdmTitleBar;
static gboolean GdmIncludeAll;
static gboolean GdmDefaultWelcome;
static gboolean GdmDefaultRemoteWelcome;
static gboolean GdmLockPosition;
static gboolean GdmBackgroundScaleToFit;
static gboolean GdmBackgroundRemoteOnlyColor;
static gboolean GdmRunBackgroundProgramAlways;
static gboolean GdmSetPosition;
static gboolean GdmQuiver;
static gboolean GdmShowGnomeFailsafe;
static gboolean GdmShowXtermFailsafe;
static gboolean GdmShowLastSession;
static gboolean GdmUse24Clock;
static gboolean GdmEntryCircles;
static gboolean GdmEntryInvisible;
static gboolean GdmGraphicalThemeRand;
static gboolean GdmBroadcast;
static gboolean GdmAllowAdd;

/**
 * gdm_config_init
 * 
 * Sets up initial hashes used by configuration routines.
 */ 
void 
gdm_config_init (void)
{
   type_hash = g_hash_table_new (g_str_hash, g_str_equal);
   val_hash  = g_hash_table_new (g_str_hash, g_str_equal);

   g_hash_table_insert (type_hash, GDM_KEY_ALLOW_REMOTE_ROOT, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ALLOW_ROOT, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ALLOW_REMOTE_AUTOLOGIN, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_PASSWORD_REQUIRED, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_AUTOMATIC_LOGIN_ENABLE, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ALWAYS_RESTART_SERVER, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ADD_GTK_MODULES, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_DOUBLE_LOGIN_WARNING, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ALWAYS_LOGIN_CURRENT_SESSION, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_DISPLAY_LAST_LOGIN, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_KILL_INIT_CLIENTS, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_CONFIG_AVAILABLE, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_SYSTEM_MENU, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_CHOOSER_BUTTON, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_BROWSER, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_MULTICAST, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_NEVER_PLACE_COOKIES_ON_NFS, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_CONSOLE_NOTIFY, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_TIMED_LOGIN_ENABLE, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ALLOW_ROOT, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_CHECK_DIR_OWNER, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_XDMCP, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_INDIRECT, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_XDMCP_PROXY, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_DYNAMIC_XSERVERS, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_VT_ALLOCATION, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_DISALLOW_TCP, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_SOUND_ON_LOGIN_SUCCESS, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_SOUND_ON_LOGIN_FAILURE, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_DEBUG, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_DEBUG_GESTURES, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ALLOW_GTK_THEME_CHANGE, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_TITLE_BAR, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_INCLUDE_ALL, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_DEFAULT_WELCOME, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_DEFAULT_REMOTE_WELCOME, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_LOCK_POSITION, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_BACKGROUND_SCALE_TO_FIT, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_BACKGROUND_REMOTE_ONLY_COLOR, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_RUN_BACKGROUND_PROGRAM_ALWAYS, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_SET_POSITION, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_QUIVER, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_SHOW_GNOME_FAILSAFE, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_SHOW_XTERM_FAILSAFE, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_SHOW_LAST_SESSION, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_USE_24_CLOCK, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ENTRY_CIRCLES, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ENTRY_INVISIBLE, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_GRAPHICAL_THEME_RAND, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_BROADCAST, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_ALLOW_ADD, &bool_type);
   g_hash_table_insert (type_hash, GDM_KEY_PATH, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_ROOT_PATH, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_CONSOLE_CANNOT_HANDLE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_CHOOSER, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GREETER, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_CONFIGURATOR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_POSTLOGIN, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_PRESESSION, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_POSTSESSION, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_FAILSAFE_XSERVER, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_X_KEEPS_CRASHING, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_BASE_XSESSION, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_REMOTE_GREETER, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_DISPLAY_INIT_DIR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_AUTOMATIC_LOGIN, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GTK_MODULES_LIST, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_REBOOT, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_HALT, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_SUSPEND, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_LOG_DIR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_PID_FILE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_FACE_DIR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_SERV_AUTHDIR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_USER_AUTHDIR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_USER_AUTHFILE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_USER_AUTHDIR_FALLBACK, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_SESSION_DESKTOP_DIR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_DEFAULT_SESSION, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_MULTICAST_ADDR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_USER, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GROUP, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GTKRC, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GTK_THEME, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_TIMED_LOGIN, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_WILLING, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_XDMCP_PROXY_XSERVER, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_XDMCP_PROXY_RECONNECT, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_STANDARD_XSERVER, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_XNEST, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_SOUND_PROGRAM, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_SOUND_ON_LOGIN_READY_FILE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GTK_THEMES_TO_ALLOW, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_INCLUDE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_EXCLUDE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_FACE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_LOCALE_FILE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_LOGO, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_CHOOSER_BUTTON_LOGO, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_WELCOME, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_REMOTE_WELCOME, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_BACKGROUND_PROGRAM, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_BACKGROUND_IMAGE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_BACKGROUND_COLOR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GRAPHICAL_THEME, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GRAPHICAL_THEME_DIR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GRAPHICAL_THEMES, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_GRAPHICAL_THEME_COLOR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_INFO_MSG_FILE, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_INFO_MSG_FONT, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_DEFAULT_HOST_IMG, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_HOST_IMAGE_DIR, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_HOSTS, &string_type);
   g_hash_table_insert (type_hash, GDM_KEY_XINERAMA_SCREEN, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_RETRY_DELAY, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_TIMED_LOGIN_DELAY, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_RELAX_PERM, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_USER_MAX_FILE, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_DISPLAYS_PER_HOST, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_MAX_PENDING, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_MAX_WAIT, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_MAX_SESSIONS, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_UDP_PORT, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_MAX_INDIRECT, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_MAX_WAIT_INDIRECT, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_PING_INTERVAL, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_FLEXIBLE_XSERVERS, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_FIRST_VT, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_POSITION_X, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_POSITION_Y, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_MINIMAL_UID, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_ICON_WIDTH, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_ICON_HEIGHT, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_BACKGROUND_TYPE, &int_type);
   g_hash_table_insert (type_hash, GDM_KEY_SCAN_TIME, &int_type);

   /* boolean values */
   g_hash_table_insert (val_hash, GDM_KEY_ALLOW_REMOTE_ROOT, &GdmAllowRemoteRoot);
   g_hash_table_insert (val_hash, GDM_KEY_ALLOW_ROOT, &GdmAllowRoot);
   g_hash_table_insert (val_hash, GDM_KEY_ALLOW_REMOTE_AUTOLOGIN, &GdmAllowRemoteAutoLogin);
   g_hash_table_insert (val_hash, GDM_KEY_PASSWORD_REQUIRED, &GdmPasswordRequired);
   g_hash_table_insert (val_hash, GDM_KEY_AUTOMATIC_LOGIN_ENABLE, &GdmAutomaticLoginEnable);
   g_hash_table_insert (val_hash, GDM_KEY_ALWAYS_RESTART_SERVER, &GdmAlwaysRestartServer);
   g_hash_table_insert (val_hash, GDM_KEY_ADD_GTK_MODULES, &GdmAddGtkModules);
   g_hash_table_insert (val_hash, GDM_KEY_DOUBLE_LOGIN_WARNING, &GdmDoubleLoginWarning);
   g_hash_table_insert (val_hash, GDM_KEY_ALWAYS_LOGIN_CURRENT_SESSION, &GdmAlwaysLoginCurrentSession);
   g_hash_table_insert (val_hash, GDM_KEY_DISPLAY_LAST_LOGIN, &GdmDisplayLastLogin);
   g_hash_table_insert (val_hash, GDM_KEY_KILL_INIT_CLIENTS, &GdmKillInitClients);
   g_hash_table_insert (val_hash, GDM_KEY_CONFIG_AVAILABLE, &GdmConfigAvailable);
   g_hash_table_insert (val_hash, GDM_KEY_SYSTEM_MENU, &GdmSystemMenu);
   g_hash_table_insert (val_hash, GDM_KEY_CHOOSER_BUTTON, &GdmChooserButton);
   g_hash_table_insert (val_hash, GDM_KEY_BROWSER, &GdmBrowser);
   g_hash_table_insert (val_hash, GDM_KEY_MULTICAST, &GdmMulticast);
   g_hash_table_insert (val_hash, GDM_KEY_NEVER_PLACE_COOKIES_ON_NFS, &GdmNeverPlaceCookiesOnNfs);
   g_hash_table_insert (val_hash, GDM_KEY_CONSOLE_NOTIFY, &GdmConsoleNotify);
   g_hash_table_insert (val_hash, GDM_KEY_TIMED_LOGIN_ENABLE, &GdmTimedLoginEnable);
   g_hash_table_insert (val_hash, GDM_KEY_CHECK_DIR_OWNER, &GdmCheckDirOwner);
   g_hash_table_insert (val_hash, GDM_KEY_XDMCP, &GdmXdmcp);
   g_hash_table_insert (val_hash, GDM_KEY_INDIRECT, &GdmIndirect);
   g_hash_table_insert (val_hash, GDM_KEY_XDMCP_PROXY, &GdmXdmcpProxy);
   g_hash_table_insert (val_hash, GDM_KEY_DYNAMIC_XSERVERS, &GdmDynamicXservers);
   g_hash_table_insert (val_hash, GDM_KEY_VT_ALLOCATION, &GdmVTAllocation);
   g_hash_table_insert (val_hash, GDM_KEY_DISALLOW_TCP, &GdmDisallowTcp);
   g_hash_table_insert (val_hash, GDM_KEY_SOUND_ON_LOGIN_SUCCESS, &GdmSoundOnLoginSuccess);
   g_hash_table_insert (val_hash, GDM_KEY_SOUND_ON_LOGIN_FAILURE, &GdmSoundOnLoginFailure);
   g_hash_table_insert (val_hash, GDM_KEY_DEBUG, &GdmDebug);
   g_hash_table_insert (val_hash, GDM_KEY_DEBUG_GESTURES, &GdmDebugGestures);
   g_hash_table_insert (val_hash, GDM_KEY_ALLOW_GTK_THEME_CHANGE, &GdmAllowGtkThemeChange);
   g_hash_table_insert (val_hash, GDM_KEY_TITLE_BAR, &GdmTitleBar);
   g_hash_table_insert (val_hash, GDM_KEY_INCLUDE_ALL, &GdmIncludeAll);
   g_hash_table_insert (val_hash, GDM_KEY_DEFAULT_WELCOME, &GdmDefaultWelcome);
   g_hash_table_insert (val_hash, GDM_KEY_DEFAULT_REMOTE_WELCOME, &GdmDefaultRemoteWelcome);
   g_hash_table_insert (val_hash, GDM_KEY_LOCK_POSITION, &GdmLockPosition);
   g_hash_table_insert (val_hash, GDM_KEY_BACKGROUND_SCALE_TO_FIT, &GdmBackgroundScaleToFit);
   g_hash_table_insert (val_hash, GDM_KEY_BACKGROUND_REMOTE_ONLY_COLOR, &GdmBackgroundRemoteOnlyColor);
   g_hash_table_insert (val_hash, GDM_KEY_RUN_BACKGROUND_PROGRAM_ALWAYS, &GdmRunBackgroundProgramAlways);
   g_hash_table_insert (val_hash, GDM_KEY_SET_POSITION, &GdmSetPosition);
   g_hash_table_insert (val_hash, GDM_KEY_QUIVER, &GdmQuiver);
   g_hash_table_insert (val_hash, GDM_KEY_SHOW_GNOME_FAILSAFE, &GdmShowGnomeFailsafe);
   g_hash_table_insert (val_hash, GDM_KEY_SHOW_XTERM_FAILSAFE, &GdmShowXtermFailsafe);
   g_hash_table_insert (val_hash, GDM_KEY_SHOW_LAST_SESSION, &GdmShowLastSession);
   g_hash_table_insert (val_hash, GDM_KEY_USE_24_CLOCK, &GdmUse24Clock);
   g_hash_table_insert (val_hash, GDM_KEY_ENTRY_CIRCLES, &GdmEntryCircles);
   g_hash_table_insert (val_hash, GDM_KEY_ENTRY_INVISIBLE, &GdmEntryInvisible);
   g_hash_table_insert (val_hash, GDM_KEY_GRAPHICAL_THEME_RAND, &GdmGraphicalThemeRand);
   g_hash_table_insert (val_hash, GDM_KEY_BROADCAST, &GdmBroadcast);
   g_hash_table_insert (val_hash, GDM_KEY_ALLOW_ADD, &GdmAllowAdd);

   /* string values */
   g_hash_table_insert (val_hash, GDM_KEY_PATH, &GdmPath);
   g_hash_table_insert (val_hash, GDM_KEY_ROOT_PATH, &GdmRootPath);
   g_hash_table_insert (val_hash, GDM_KEY_CONSOLE_CANNOT_HANDLE, &GdmConsoleCannotHandle);
   g_hash_table_insert (val_hash, GDM_KEY_CHOOSER, &GdmChooser);
   g_hash_table_insert (val_hash, GDM_KEY_GREETER, &GdmGreeter);
   g_hash_table_insert (val_hash, GDM_KEY_CONFIGURATOR, &GdmConfigurator);
   g_hash_table_insert (val_hash, GDM_KEY_POSTLOGIN, &GdmPostLogin);
   g_hash_table_insert (val_hash, GDM_KEY_PRESESSION, &GdmPreSession);
   g_hash_table_insert (val_hash, GDM_KEY_POSTSESSION, &GdmPostSession);
   g_hash_table_insert (val_hash, GDM_KEY_FAILSAFE_XSERVER, &GdmFailsafeXserver);
   g_hash_table_insert (val_hash, GDM_KEY_X_KEEPS_CRASHING, &GdmXKeepsCrashing);
   g_hash_table_insert (val_hash, GDM_KEY_BASE_XSESSION, &GdmBaseXsession);
   g_hash_table_insert (val_hash, GDM_KEY_REMOTE_GREETER, &GdmRemoteGreeter);
   g_hash_table_insert (val_hash, GDM_KEY_DISPLAY_INIT_DIR, &GdmInitDir);
   g_hash_table_insert (val_hash, GDM_KEY_AUTOMATIC_LOGIN, &GdmAutomaticLogin);
   g_hash_table_insert (val_hash, GDM_KEY_GTK_MODULES_LIST, &GdmGtkModulesList);
   g_hash_table_insert (val_hash, GDM_KEY_REBOOT, &GdmReboot);
   g_hash_table_insert (val_hash, GDM_KEY_HALT, &GdmHalt);
   g_hash_table_insert (val_hash, GDM_KEY_SUSPEND, &GdmSuspend);
   g_hash_table_insert (val_hash, GDM_KEY_LOG_DIR, &GdmLogDir);
   g_hash_table_insert (val_hash, GDM_KEY_PID_FILE, &GdmPidFile);
   g_hash_table_insert (val_hash, GDM_KEY_FACE_DIR, &GdmFaceDir);
   g_hash_table_insert (val_hash, GDM_KEY_SERV_AUTHDIR, &GdmServAuthDir);
   g_hash_table_insert (val_hash, GDM_KEY_USER_AUTHDIR, &GdmUserAuthDir);
   g_hash_table_insert (val_hash, GDM_KEY_USER_AUTHFILE, &GdmUserAuthFile);
   g_hash_table_insert (val_hash, GDM_KEY_USER_AUTHDIR_FALLBACK, &GdmUserAuthFallback);
   g_hash_table_insert (val_hash, GDM_KEY_SESSION_DESKTOP_DIR, &GdmSessDir);
   g_hash_table_insert (val_hash, GDM_KEY_DEFAULT_SESSION, &GdmDefaultSession);
   g_hash_table_insert (val_hash, GDM_KEY_MULTICAST_ADDR, &GdmMulticastAddr);
   g_hash_table_insert (val_hash, GDM_KEY_USER, &GdmUser);
   g_hash_table_insert (val_hash, GDM_KEY_GROUP, &GdmGroup);
   g_hash_table_insert (val_hash, GDM_KEY_GTKRC, &GdmGtkRc);
   g_hash_table_insert (val_hash, GDM_KEY_GTK_THEME, &GdmGtkTheme);
   g_hash_table_insert (val_hash, GDM_KEY_TIMED_LOGIN, &GdmTimedLogin);
   g_hash_table_insert (val_hash, GDM_KEY_WILLING, &GdmWilling);
   g_hash_table_insert (val_hash, GDM_KEY_XDMCP_PROXY_XSERVER, &GdmXdmcpProxyXserver);
   g_hash_table_insert (val_hash, GDM_KEY_XDMCP_PROXY_RECONNECT, &GdmXdmcpProxyReconnect);
   g_hash_table_insert (val_hash, GDM_KEY_STANDARD_XSERVER, &GdmStandardXserver);
   g_hash_table_insert (val_hash, GDM_KEY_XNEST, &GdmXnest);
   g_hash_table_insert (val_hash, GDM_KEY_SOUND_PROGRAM, &GdmSoundProgram);
   g_hash_table_insert (val_hash, GDM_KEY_SOUND_ON_LOGIN_READY_FILE, &GdmSoundOnLoginReadyFile);
   g_hash_table_insert (val_hash, GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE, &GdmSoundOnLoginSuccessFile);
   g_hash_table_insert (val_hash, GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE, &GdmSoundOnLoginFailureFile);
   g_hash_table_insert (val_hash, GDM_KEY_GTK_THEMES_TO_ALLOW, &GdmGtkThemesToAllow);
   g_hash_table_insert (val_hash, GDM_KEY_INCLUDE, &GdmInclude);
   g_hash_table_insert (val_hash, GDM_KEY_EXCLUDE, &GdmExclude);
   g_hash_table_insert (val_hash, GDM_KEY_FACE, &GdmFace);
   g_hash_table_insert (val_hash, GDM_KEY_LOCALE_FILE, &GdmLocaleFile);
   g_hash_table_insert (val_hash, GDM_KEY_LOGO, &GdmLogo);
   g_hash_table_insert (val_hash, GDM_KEY_CHOOSER_BUTTON_LOGO, &GdmChooserButtonLogo);
   g_hash_table_insert (val_hash, GDM_KEY_WELCOME, &GdmWelcome);
   g_hash_table_insert (val_hash, GDM_KEY_REMOTE_WELCOME, &GdmRemoteWelcome);
   g_hash_table_insert (val_hash, GDM_KEY_BACKGROUND_PROGRAM, &GdmBackgroundProgram);
   g_hash_table_insert (val_hash, GDM_KEY_BACKGROUND_IMAGE, &GdmBackgroundImage);
   g_hash_table_insert (val_hash, GDM_KEY_BACKGROUND_COLOR, &GdmBackgroundColor);
   g_hash_table_insert (val_hash, GDM_KEY_GRAPHICAL_THEME, &GdmGraphicalTheme);
   g_hash_table_insert (val_hash, GDM_KEY_GRAPHICAL_THEME_DIR, &GdmGraphicalThemeDir);
   g_hash_table_insert (val_hash, GDM_KEY_GRAPHICAL_THEMES, &GdmGraphicalThemes);
   g_hash_table_insert (val_hash, GDM_KEY_GRAPHICAL_THEME_COLOR, &GdmGraphicalThemeColor);
   g_hash_table_insert (val_hash, GDM_KEY_INFO_MSG_FILE, &GdmInfoMsgFile);
   g_hash_table_insert (val_hash, GDM_KEY_INFO_MSG_FONT, &GdmInfoMsgFont);
   g_hash_table_insert (val_hash, GDM_KEY_DEFAULT_HOST_IMG, &GdmHost);
   g_hash_table_insert (val_hash, GDM_KEY_HOST_IMAGE_DIR, &GdmHostImageDir);
   g_hash_table_insert (val_hash, GDM_KEY_HOSTS, &GdmHosts);

   /* int values */
   g_hash_table_insert (val_hash, GDM_KEY_XINERAMA_SCREEN, &GdmXineramaScreen);
   g_hash_table_insert (val_hash, GDM_KEY_RETRY_DELAY, &GdmRetryDelay);
   g_hash_table_insert (val_hash, GDM_KEY_TIMED_LOGIN_DELAY, &GdmTimedLoginDelay);
   g_hash_table_insert (val_hash, GDM_KEY_RELAX_PERM, &GdmRelaxPerm);
   g_hash_table_insert (val_hash, GDM_KEY_USER_MAX_FILE, &GdmUserMaxFile);
   g_hash_table_insert (val_hash, GDM_KEY_DISPLAYS_PER_HOST, &GdmDisplaysPerHost);
   g_hash_table_insert (val_hash, GDM_KEY_MAX_PENDING, &GdmMaxPending);
   g_hash_table_insert (val_hash, GDM_KEY_MAX_WAIT, &GdmMaxWait);
   g_hash_table_insert (val_hash, GDM_KEY_MAX_SESSIONS, &GdmMaxSessions);
   g_hash_table_insert (val_hash, GDM_KEY_UDP_PORT, &GdmUdpPort);
   g_hash_table_insert (val_hash, GDM_KEY_MAX_INDIRECT, &GdmMaxIndirect);
   g_hash_table_insert (val_hash, GDM_KEY_MAX_WAIT_INDIRECT, &GdmMaxWaitIndirect);
   g_hash_table_insert (val_hash, GDM_KEY_PING_INTERVAL, &GdmPingInterval);
   g_hash_table_insert (val_hash, GDM_KEY_FLEXIBLE_XSERVERS, &GdmFlexibleXservers);
   g_hash_table_insert (val_hash, GDM_KEY_FIRST_VT, &GdmFirstVt);
   g_hash_table_insert (val_hash, GDM_KEY_POSITION_X, &GdmPositionX);
   g_hash_table_insert (val_hash, GDM_KEY_POSITION_Y, &GdmPositionY);
   g_hash_table_insert (val_hash, GDM_KEY_MINIMAL_UID, &GdmMinimalUid);
   g_hash_table_insert (val_hash, GDM_KEY_ICON_WIDTH, &GdmIconWidth);
   g_hash_table_insert (val_hash, GDM_KEY_ICON_HEIGHT, &GdmIconHeight);
   g_hash_table_insert (val_hash, GDM_KEY_BACKGROUND_TYPE, &GdmBackgroundType);
   g_hash_table_insert (val_hash, GDM_KEY_SCAN_TIME, &GdmScanTime);
}

/**
 * gdm_get_config:
 *
 * Get config file.  
 */
static VeConfig *
gdm_get_config (struct stat *statbuf)
{
    int r;

    /* Not NULL if config_file was set by command-line option. */
    if (config_file != NULL) {
       VE_IGNORE_EINTR (r = stat (config_file, statbuf));
    } else {
       /* First check sysconfdir */
       VE_IGNORE_EINTR (r = stat (GDM_SYSCONFDIR_CONFIG_FILE, statbuf));
       if (r < 0) {
          gdm_error (_("%s: No GDM configuration file: %s. Using defaults."),
                       "gdm_config_parse", GDM_SYSCONFDIR_CONFIG_FILE);
       } else {
               config_file = GDM_SYSCONFDIR_CONFIG_FILE;
       }
    }
    return ve_config_new (config_file);
}

/**
 * gdm_get_value_int
 *
 * Gets an integer configuration option by key.  The option must
 * first be loaded, say, by calling gdm_config_parse.
 */
gint
gdm_get_value_int (char *key)
{
   GdmConfigType *type = g_hash_table_lookup (type_hash, key);
   gpointer val = g_hash_table_lookup (val_hash, key);

   if (type == NULL || val == NULL) {
      gdm_error ("Request for invalid configuration key %s", key);
   } else if (*type != CONFIG_INT) {
      gdm_error ("Request for configuration key %s, but not type INT", key);
   } else {
      gint *intval = (int *)val;
      return *intval;
   }

   return 0;
}

/**
 * gdm_get_value_string
 *
 * Gets a string configuration option by key.  The option must
 * first be loaded, say, by calling gdm_config_parse.
 */
gchar *
gdm_get_value_string (char *key)
{
   GdmConfigType *type = g_hash_table_lookup (type_hash, key);
   gpointer val = g_hash_table_lookup (val_hash, key);

   if (type == NULL || val == NULL) {
      gdm_error ("Request for invalid configuration key %s", key);
   } else if (*type != CONFIG_STRING) {
      gdm_error ("Request for configuration key %s, but not type STRING", key);
   } else {
      gchar **charval = (char **)val;
      return *charval;
   }

   return NULL;
}

/**
 * gdm_get_value_bool
 * 
 * Gets a boolean configuration option by key.  The option must
 * first be loaded, say, by calling gdm_config_parse.
 */
gboolean
gdm_get_value_bool (char *key)
{
   GdmConfigType *type = g_hash_table_lookup (type_hash, key);
   gpointer val = g_hash_table_lookup (val_hash, key);

   if (type == NULL || val == NULL) {
      gdm_error ("Request for invalid configuration key %s", key);
   } else if (*type != CONFIG_BOOL) {
      gdm_error ("Request for configuration key %s, but not type BOOL", key);
   } else {
      gboolean *boolval = (gboolean *)val;
      return *boolval;
   }

   return FALSE;
}

/**
 * gdm_config_to_string
 *
 * Returns a configuration option as a string.  Used by GDM's
 * GET_CONFIG socket command.
 */ 
void
gdm_config_to_string (gchar *key, gchar **retval)
{
   GdmConfigType *type = g_hash_table_lookup (type_hash, key);

   *retval = NULL;

   if (type != NULL) {
      if (*type == CONFIG_BOOL) {
         gboolean value = gdm_get_value_bool (key);
         if (value)
            *retval = g_strdup ("true");
         else
            *retval = g_strdup ("false");
         return;
      } else if (*type == CONFIG_INT) {
         gint value = gdm_get_value_int (key);
         *retval = g_strdup_printf ("%d", value);
         return;
      } else if (*type == CONFIG_STRING) {
         gchar *value = gdm_get_value_string (key);
         if (value != NULL)
            *retval = g_strdup (value);
         return;
      }
   }

   gdm_debug ("Error returning config key %s\n", key);
   return;
}

/**
 * is_key
 *
 * Since GDM keys sometimes have default values defined in the gdm.h header
 * file (e.g. key=value), this function strips off the "=value" from both 
 * keys passed in to do a comparison.
 */
static gboolean
is_key (const gchar *key1, const gchar *key2)
{
   gchar *key1d, *key2d, *p;

   key1d = g_strdup (key1);
   key2d = g_strdup (key2);

   g_strstrip (key1d);
   p = strchr (key1d, '=');
   if (p != NULL)
      *p = '\0';

   g_strstrip (key2d);
   p = strchr (key2d, '=');
   if (p != NULL)
      *p = '\0';

   if (strcmp (key1d, key2d) == 0) {
      g_free (key1d);
      g_free (key2d);
      return TRUE;
   } else {
      g_free (key1d);
      g_free (key2d);
      return FALSE;
   }
}

/**
 * gdm_compare_displays
 * 
 * Support function for loading displays from the gdm.conf file
 */
int
gdm_compare_displays (gconstpointer a, gconstpointer b)
{
   const GdmDisplay *d1 = a;
   const GdmDisplay *d2 = b;
   if (d1->dispnum < d2->dispnum)
      return -1;
   else if (d1->dispnum > d2->dispnum)
      return 1;
   else
      return 0;
}

/**
 * notify_displays_int
 * notify_displays_string
 *
 * The following two functions will notify the slave programs
 * (gdmgreeter, gdmlogin, etc.) that a configuration option has
 * been changed so the slave can update with the new option
 * value.  GDM does this notify when it receives a
 * GDM_CONFIG_UPDATE socket command from gdmsetup or from the
 * gdmflexiserver --command option.   notify_displays_int
 * is also used for notifying for boolean values.
 */
static void
notify_displays_int (const gchar *key, int val)
{
   GSList *li;
   for (li = displays; li != NULL; li = li->next) {
       GdmDisplay *disp = li->data;
       if (disp->master_notify_fd >= 0) {
          gdm_fdprintf (disp->master_notify_fd, "%c%s %d\n",
                        GDM_SLAVE_NOTIFY_KEY, key, val);

          if (disp != NULL && disp->slavepid > 1)
             kill (disp->slavepid, SIGUSR2);
      }
   }
}

/* If id == NULL, then get the first X server */
GdmXserver *
gdm_find_x_server (const gchar *id)
{
   GSList *li;

   if (xservers == NULL)
      return NULL;

   if (id == NULL)
      return xservers->data;

   for (li = xservers; li != NULL; li = li->next) {
      GdmXserver *svr = li->data;
      if (strcmp (svr->id, id) == 0)
         return svr;
   }
   return NULL;
}

static void
notify_displays_string (const gchar *key, const gchar *val)
{
   GSList *li;
   for (li = displays; li != NULL; li = li->next) {
      GdmDisplay *disp = li->data;
      if (disp->master_notify_fd >= 0) {
         gdm_fdprintf (disp->master_notify_fd, "%c%s %s\n",
                       GDM_SLAVE_NOTIFY_KEY, key, val);
         if (disp != NULL && disp->slavepid > 1)
            kill (disp->slavepid, SIGUSR2);
      }
   }
}

/* TODO - Need to fix so notification happens for TIMED_LOGIN */

/**
 * gdm_set_value_string
 * gdm_set_value_bool
 * gdm_set_value_int
 *
 * The following interfaces are used to set values.  The private
 * static interfaces have a "doing_update" boolean argument which is
 * only set when GDM_UPDATE_CONFIG is called.  If doing_update is
 * TRUE, then a notify is sent to slaves.  When loading values
 * at other times (such as when first loading configuration options)
 * there is no need to notify the slaves.  If there is a desire to
 * send a notify to the slaves, the gdm_update_config function
 * should be used.
 */
static void
_gdm_set_value_string (gchar *key, gchar *value, gboolean doing_update)
{
   gchar **setting = g_hash_table_lookup (val_hash, key);
   gchar *setting_copy;
   gchar *temp_string;

   if (setting == NULL) {
      gdm_error ("Failure setting key %s to %s", key, value);
      return;
   }

   setting_copy = g_strdup (*setting);
   g_free (*setting);

   if (is_key (key, GDM_KEY_PATH)) {

      temp_string = gdm_read_default ("PATH=");
      if (temp_string != NULL)
         *setting = temp_string;                
      else if (value == NULL)
         *setting = NULL;
      else
         *setting = g_strdup (value);

   } else if (is_key (key, GDM_KEY_ROOT_PATH)) {

      temp_string = gdm_read_default ("SUPATH=");
      if (temp_string != NULL)
         *setting = temp_string;                
      else if (value == NULL)
         *setting = NULL;
      else
         *setting = g_strdup (value);

    } else if (is_key (key, GDM_KEY_BASE_XSESSION)) {
       if (! ve_string_empty (value)) {
          *setting = g_strdup (value);
       } else {
          gdm_info (_("%s: BaseXsession empty; using %s/gdm/Xsession"),
                      "gdm_config_parse",
                      EXPANDED_SYSCONFDIR);
          *setting = g_build_filename (EXPANDED_SYSCONFDIR, "gdm", "Xsession", NULL);
       }
   } else if (is_key (key, GDM_KEY_HALT) ||
              is_key (key, GDM_KEY_REBOOT) ||
              is_key (key, GDM_KEY_SUSPEND)) {
       if (value == NULL)
          *setting = NULL;
       else
          *setting = g_strdup (value);
   } else if (is_key (key, GDM_KEY_CONSOLE_CANNOT_HANDLE)) {
       if (value == NULL)
          *setting = "";
       else
          *setting = g_strdup (value);
       gdm_ok_console_language ();
   } else if (is_key (key, GDM_KEY_STANDARD_XSERVER)) {
      gchar *bin;
      bin = ve_first_word (value);
      if G_UNLIKELY (ve_string_empty (bin) ||
                     access (bin, X_OK) != 0) {
         gdm_info (_("%s: Standard X server not found; trying alternatives"),
                     "gdm_config_parse");
         if (access ("/usr/X11R6/bin/X", X_OK) == 0) {
            *setting = g_strdup ("/usr/X11R6/bin/X");
         } else if (access ("/opt/X11R6/bin/X", X_OK) == 0) {
            *setting = g_strdup ("/opt/X11R6/bin/X");
         } else if (ve_string_empty (GdmStandardXserver)) {
            *setting = g_strdup ("/usr/bin/X11/X");
         } else
            *setting = g_strdup (value);
      }
   } else {
       if (! ve_string_empty (value))
          *setting = g_strdup (value);
       else {
          if (is_key (key, GDM_KEY_GREETER))
             gdm_error (_("%s: No greeter specified."), "gdm_config_parse");
          else if (is_key (key, GDM_KEY_REMOTE_GREETER))
             gdm_error (_("%s: No remote greeter specified."), "gdm_config_parse");
          else if (is_key (key, GDM_KEY_SESSION_DESKTOP_DIR))
             gdm_error (_("%s: No sessions directory specified."), "gdm_config_parse");

          *setting = NULL;
       }
   }

   if (doing_update == TRUE && strcmp (*setting, setting_copy) != 0) {
      if (is_key (key, GDM_KEY_GREETER))
         notify_displays_string (GDM_NOTIFY_GREETER, *setting);
      else if (is_key (key, GDM_KEY_REMOTE_GREETER))
         notify_displays_string (GDM_NOTIFY_REMOTE_GREETER, *setting);
      else if (is_key (key, GDM_KEY_SOUND_ON_LOGIN_READY_FILE))
         notify_displays_string (GDM_NOTIFY_SOUND_ON_LOGIN_READY_FILE, *setting);
      else if (is_key (key, GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE))
         notify_displays_string (GDM_NOTIFY_SOUND_ON_LOGIN_SUCCESS_FILE, *setting);
      else if (is_key (key, GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE))
         notify_displays_string (GDM_NOTIFY_SOUND_ON_LOGIN_FAILURE_FILE, *setting);
      else if (is_key (key, GDM_KEY_GTK_MODULES_LIST))
         notify_displays_string (GDM_NOTIFY_GTK_MODULES_LIST, *setting);
   }
   g_free (setting_copy);
   if (*setting == NULL)
      gdm_debug ("set config key %s to string <NULL>", key);
   else
      gdm_debug ("set config key %s to string %s", key, *setting);
}

void
gdm_set_value_string (gchar *key, gchar *value)
{
   _gdm_set_value_string (key, value, FALSE);
}

static void
_gdm_set_value_bool (gchar *key, gboolean value, gboolean doing_update)
{
   gboolean *setting     = g_hash_table_lookup (val_hash, key);
   gboolean setting_copy = *setting;
   gchar *temp_string;

   if (setting == NULL) {
      if (value)
         gdm_error ("Failure setting key %s to true", key);
      else
         gdm_error ("Failure setting key %s to false", key);
      return;
   }

   if (is_key (key, GDM_KEY_PASSWORD_REQUIRED)) {
      temp_string = gdm_read_default ("PASSREQ=");
      if (temp_string == NULL)
         *setting = value;
      else if (g_ascii_strcasecmp (temp_string, "YES") == 0)
         *setting = TRUE;
      else
         *setting = FALSE;
      g_free (temp_string);
   } else if (is_key (key, GDM_KEY_ALLOW_REMOTE_ROOT)) {
      temp_string = gdm_read_default ("CONSOLE=");

      if (temp_string == NULL)
         *setting = value;
      else if (g_ascii_strcasecmp (temp_string, "/dev/console") != 0)
         *setting = TRUE;
      else
         *setting = FALSE;
      g_free (temp_string);
#ifndef HAVE_LIBXDMCP
   } else if (is_key (key, GDM_KEY_XMDCP)) {
      if (value) {
         gdm_info (_("%s: XDMCP was enabled while there is no XDMCP support; turning it off"), "gdm_config_parse");
      }
      *setting = FALSE;
#endif
   } else {
      *setting = value;
   }

   if (doing_update == TRUE && *setting != setting_copy) {
     if (is_key (key, GDM_KEY_ALLOW_ROOT))
        notify_displays_int (GDM_NOTIFY_ALLOW_ROOT, *setting);
     else if (is_key (key, GDM_KEY_ALLOW_REMOTE_ROOT))
        notify_displays_int (GDM_NOTIFY_ALLOW_REMOTE_ROOT, *setting);
     else if (is_key (key, GDM_KEY_ALLOW_REMOTE_AUTOLOGIN))
        notify_displays_int (GDM_NOTIFY_ALLOW_REMOTE_AUTOLOGIN, *setting);
     else if (is_key (key, GDM_KEY_SYSTEM_MENU))
        notify_displays_int (GDM_NOTIFY_SYSTEM_MENU, *setting);
     else if (is_key (key, GDM_KEY_CONFIG_AVAILABLE))
        notify_displays_int (GDM_NOTIFY_CONFIG_AVAILABLE, *setting);
     else if (is_key (key, GDM_KEY_CHOOSER_BUTTON))
        notify_displays_int (GDM_NOTIFY_CHOOSER_BUTTON, *setting); 
     else if (is_key (key, GDM_KEY_DISALLOW_TCP))
        notify_displays_int (GDM_NOTIFY_DISALLOW_TCP, *setting);
     else if (is_key (key, GDM_KEY_ADD_GTK_MODULES))
        notify_displays_int (GDM_NOTIFY_ADD_GTK_MODULES, *setting);
   }

   if (*setting)
      gdm_debug ("set config key %s to boolean true", key);
   else
      gdm_debug ("set config key %s to boolean false", key);
}

void
gdm_set_value_bool (gchar *key, gboolean value)
{
   _gdm_set_value_bool (key, value, FALSE);
}

void
_gdm_set_value_int (gchar *key, gint value, gboolean doing_update)
{
   gint *setting     = g_hash_table_lookup (val_hash, key);
   gint setting_copy = *setting;

   if (setting == NULL) {
      gdm_error ("Failure setting key %s to %d", key, value);
      return;
   }

   if (is_key (key, GDM_KEY_TIMED_LOGIN_DELAY)) {
      if (value < 5) {
         gdm_info (_("%s: TimedLoginDelay is less than 5, defaulting to 5."), "gdm_config_parse");
         *setting = 5;
      } else
         *setting = value;
   } else if (is_key (key, GDM_KEY_MAX_INDIRECT)) {
      if (value < 0)
         *setting = 0;
      else
         *setting = value;
   } else {
      *setting = value;
   }
 
   if (doing_update == TRUE && *setting != setting_copy) {
      if (is_key (key, GDM_KEY_RETRY_DELAY))
         notify_displays_int (GDM_NOTIFY_RETRY_DELAY, *setting);
      else if (is_key (key, GDM_KEY_TIMED_LOGIN_DELAY))
         notify_displays_int (GDM_NOTIFY_TIMED_LOGIN_DELAY, *setting);
   }

   gdm_debug ("set config key %s to integer %d", key, *setting);
}

void
gdm_set_value_int (gchar *key, gint value)
{
   _gdm_set_value_int (key, value, FALSE);
}

/**
 * gdm_update_config
 * 
 * Will cause a the GDM daemon to re-read the key from the configuration
 * file and cause notify signal to be sent to the slaves for the 
 * specified key, if appropriate.  Only specific keys defined in the
 * gdm_set_value functions above are associated with such notification.
 * Obviously notification is not needed for configuration options only
 * used by the daemon.
 *
 * To add a new notification, a GDM_NOTIFY_* argument will need to be
 * defined in gdm.h, supporting logic placed in the gdm_set_value
 * functions and in the gdm_slave_handle_notify function in slave.c.
 */
gboolean
gdm_update_config (gchar* key)
{
   GdmConfigType *type = g_hash_table_lookup (type_hash, key);
   struct stat statbuf;
   VeConfig* cfg;
   int r;
   gboolean rc;

   /*
    * Do not allow these keys to be updated, since GDM would need
    * additional work, or at least heavy testing, to make these keys
    * flexible enough to be changed at runtime.
    */ 
   if (type == NULL ||
       is_key (key, GDM_KEY_PID_FILE) ||
       is_key (key, GDM_KEY_CONSOLE_NOTIFY) ||
       is_key (key, GDM_KEY_USER) ||
       is_key (key, GDM_KEY_GROUP) ||
       is_key (key, GDM_KEY_LOG_DIR) ||
       is_key (key, GDM_KEY_SERV_AUTHDIR) ||
       is_key (key, GDM_KEY_USER_AUTHDIR) ||
       is_key (key, GDM_KEY_USER_AUTHFILE) ||
       is_key (key, GDM_KEY_USER_AUTHDIR_FALLBACK)) {
      return FALSE;
   }

   /* Shortcut for updating all XDMCP parameters */
   if (is_key (key, "xdmcp/PARAMETERS")) {
      gdm_update_config (GDM_KEY_DISPLAYS_PER_HOST);
      gdm_update_config (GDM_KEY_MAX_PENDING);
      gdm_update_config (GDM_KEY_MAX_WAIT);
      gdm_update_config (GDM_KEY_MAX_SESSIONS);
      gdm_update_config (GDM_KEY_INDIRECT);
      gdm_update_config (GDM_KEY_MAX_INDIRECT);
      gdm_update_config (GDM_KEY_MAX_WAIT_INDIRECT);
      gdm_update_config (GDM_KEY_PING_INTERVAL);
   }

   VE_IGNORE_EINTR (r = stat (config_file, &statbuf));
   if G_UNLIKELY (r < 0) {
      /* if the file didn't exist before either */
      if (config_file_mtime == 0)
         return TRUE;
   } else {
     if (config_file_mtime == statbuf.st_mtime)
         return TRUE;
      config_file_mtime = statbuf.st_mtime;
   }

   cfg = ve_config_new (config_file);

   if (*type == CONFIG_BOOL) {
      gboolean value = ve_config_get_bool (cfg, key);
      _gdm_set_value_bool (key, value, TRUE);
   } else if (*type == CONFIG_INT) {
      gint value = ve_config_get_int (cfg, key);
      _gdm_set_value_int (key, value, TRUE);
   } else if (*type == CONFIG_STRING) {
      gchar *value = ve_config_get_string (cfg, key);
      _gdm_set_value_string (key, value, TRUE);
   } else {
      ve_config_destroy (cfg);
      return FALSE;
   }

   ve_config_destroy (cfg);
   return TRUE;
}

/**
 * check_logdir
 * check_servauthdir
 * display_exists
 *
 * Support functions for gdm_config_parse.
 */
static void
check_logdir (void)
{
        struct stat statbuf;
        int r;

        VE_IGNORE_EINTR (r = stat (GdmLogDir, &statbuf));
        if (r < 0 ||
            ! S_ISDIR (statbuf.st_mode))  {
                gdm_error (_("%s: Logdir %s does not exist or isn't a directory.  Using ServAuthDir %s."), "gdm_config_parse",
                           GdmLogDir, GdmServAuthDir);
                g_free (GdmLogDir);
                GdmLogDir = g_strdup (GdmServAuthDir);
        }
}

static void
check_servauthdir (struct stat *statbuf)
{
    int r;

    /* Enter paranoia mode */
    VE_IGNORE_EINTR (r = stat (GdmServAuthDir, statbuf));
    if G_UNLIKELY (r < 0) {
            gchar *s = g_strdup_printf
                    (C_(N_("Server Authorization directory "
                           "(daemon/ServAuthDir) is set to %s "
                           "but this does not exist. Please "
                           "correct GDM configuration and "
                           "restart GDM.")), GdmServAuthDir);
        if (GdmConsoleNotify)
                    gdm_text_message_dialog (s);
            GdmPidFile = NULL;
            g_free (s);
            gdm_fail (_("%s: Authdir %s does not exist. Aborting."), "gdm_config_parse", GdmServAuthDir);
    }

    if G_UNLIKELY (! S_ISDIR (statbuf->st_mode)) {
            gchar *s = g_strdup_printf
                    (C_(N_("Server Authorization directory "
                           "(daemon/ServAuthDir) is set to %s "
                           "but this is not a directory. Please "
                           "correct GDM configuration and "
                           "restart GDM.")), GdmServAuthDir);
        if (GdmConsoleNotify)
                    gdm_text_message_dialog (s);
            GdmPidFile = NULL;
            g_free (s);
            gdm_fail (_("%s: Authdir %s is not a directory. Aborting."), "gdm_config_parse", GdmServAuthDir);
    }
}

static gboolean
display_exists (int num)
{
        GSList *li;

        for (li = displays; li != NULL; li = li->next) {
                GdmDisplay *disp = li->data;
                if (disp->dispnum == num)
                        return TRUE;
        }
        return FALSE;
}

/**
 * gdm_load_config_option
 *
 * Called by gdm_config_parse in a loop to set each key.
 */
void
gdm_load_config_option (gpointer key_in, gpointer value_in, gpointer data)
{
   gchar *key = (gchar *)key_in;
   GdmConfigType *type = (GdmConfigType *)value_in;
   VeConfig *cfg = (VeConfig *)data;

   if (type != NULL) {
      if (type != NULL && *type == CONFIG_BOOL) {
          gboolean value = ve_config_get_bool (cfg, key);
          gdm_set_value_bool (key, value);
          return;
      } else if (type != NULL && *type == CONFIG_INT) {
          gint value = ve_config_get_int (cfg, key);
          gdm_set_value_int (key, value);
          return;
      } else if (type != NULL && *type == CONFIG_STRING) {
          gchar *value = ve_config_get_string (cfg, key);
          gdm_set_value_string (key, value);
          return;
      }
   }

   gdm_error ("Cannot set config option %s", key); 
}

/**
 * gdm_config_parse
 *
 * Loads initial configuration settings.
 */
void
gdm_config_parse (void)
{
   VeConfig* cfg;
   struct passwd *pwent;
   struct group *grent;
   struct stat statbuf;
   gchar *bin;
   GList *list, *li;
   int r;

   gdm_config_init ();

   displays          = NULL;
   high_display_num  = 0;
   cfg               = gdm_get_config (&statbuf);
   config_file_mtime = statbuf.st_mtime;

   /* Loop over all configuration options and load them */
   g_hash_table_foreach (type_hash, gdm_load_config_option, cfg);

   /* Find server definitions */
   list = ve_config_get_sections (cfg);
   for (li = list; li != NULL; li = li->next) {
      const gchar *sec = li->data;
      if (strncmp (sec, "server-", strlen ("server-")) == 0) {
         GdmXserver *svr = g_new0 (GdmXserver, 1);
         gchar buf[256];

         svr->id = g_strdup (sec + strlen ("server-"));
         g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_NAME, sec);
         svr->name = ve_config_get_string (cfg, buf);
         g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_COMMAND, sec);
         svr->command = ve_config_get_string (cfg, buf);
         g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_FLEXIBLE, sec);
         svr->flexible = ve_config_get_bool (cfg, buf);
         g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_CHOOSABLE, sec);
         svr->choosable = ve_config_get_bool (cfg, buf);
         g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_HANDLED, sec);
         svr->handled = ve_config_get_bool (cfg, buf);
         g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_CHOOSER, sec);
         svr->chooser = ve_config_get_bool (cfg, buf);

         if (ve_string_empty (svr->command)) {
            gdm_error (_("%s: Empty server command; "
                         "using standard command."), "gdm_config_parse");
            g_free (svr->command);
            svr->command = g_strdup (GdmStandardXserver);
         }

         xservers = g_slist_append (xservers, svr);
      }
   }
  ve_config_free_list_of_strings (list);

  if (xservers == NULL ||
     gdm_find_x_server (GDM_STANDARD) == NULL) {
        GdmXserver *svr = g_new0 (GdmXserver, 1);

        svr->id = g_strdup (GDM_STANDARD);
        svr->name = g_strdup ("Standard server");
        svr->command = g_strdup (GdmStandardXserver);
        svr->flexible = TRUE;
        svr->choosable = TRUE;
        svr->handled = TRUE;

        xservers = g_slist_append (xservers, svr);
  }

  /* Find static X server definitions */
  list = ve_config_get_keys (cfg, GDM_KEY_SECTION_SERVERS);
  /* only read the list if no_console is FALSE
     at this stage */
  for (li = list; ! no_console && li != NULL; li = li->next) {
     const gchar *key = li->data;
     if (isdigit (*key)) {
        gchar *full;
        gchar *val;
        int disp_num = atoi (key);
        GdmDisplay *disp;

        while (display_exists (disp_num)) {
           disp_num++;
        }

        if (disp_num != atoi (key)) {
           gdm_error (_("%s: Display number %d in use!  Defaulting to %d"),
                        "gdm_config_parse", atoi (key), disp_num);
        }

        full = g_strdup_printf ("%s/%s", GDM_KEY_SECTION_SERVERS, key);
        val  = ve_config_get_string (cfg, full);
        g_free (full);

        disp = gdm_server_alloc (disp_num, val);
        g_free (val);

        if (disp == NULL)
           continue;
        displays = g_slist_insert_sorted (displays, disp, gdm_compare_displays);
        if (disp_num > high_display_num)
           high_display_num = disp_num;
        } else {
          gdm_info (_("%s: Invalid server line in config file. Ignoring!"), "gdm_config_parse");
        }
    }
    ve_config_free_list_of_strings (list);

    if G_UNLIKELY ((displays == NULL) && (! GdmXdmcp) && (!GdmDynamicXservers)) {
       gchar *server = NULL;

       /* if we requested no static servers (there is no console),
          then don't display errors in console messages */
       if (no_console) {
          gdm_fail (_("%s: XDMCP disabled and no static servers defined. Aborting!"), "gdm_config_parse");
       }

       bin = ve_first_word (GdmStandardXserver);
       if G_LIKELY (access (bin, X_OK) == 0) {
          server = GdmStandardXserver;
       } else if (access ("/usr/bin/X11/X", X_OK) == 0) {
          server = "/usr/bin/X11/X";
       } else if (access ("/usr/X11R6/bin/X", X_OK) == 0) {
          server = "/usr/X11R6/bin/X";
       } else if (access ("/opt/X11R6/bin/X", X_OK) == 0) {
          server = "/opt/X11R6/bin/X";
       }
       g_free (bin);

       /* yay, we can add a backup emergency server */
       if (server != NULL) {
          int num = gdm_get_free_display (0 /* start */, 0 /* server uid */);
          gdm_error (_("%s: XDMCP disabled and no static servers defined. Adding %s on :%d to allow configuration!"),
                       "gdm_config_parse", server, num);

          gdm_emergency_server = TRUE;
          displays = g_slist_append (displays, gdm_server_alloc (num, server));
          /* ALWAYS run the greeter and don't log anyone in,
           * this is just an emergency session */
          g_free (GdmAutomaticLogin);
          GdmAutomaticLogin = NULL;
          g_free (GdmTimedLogin);
          GdmTimedLogin = NULL;
       } else {
          gchar *s = g_strdup_printf (C_(N_("XDMCP is disabled and GDM "
                                    "cannot find any static server "
                                    "to start.  Aborting!  Please "
                                    "correct the configuration "
                                    "and restart GDM.")));
          gdm_text_message_dialog (s);
          GdmPidFile = NULL;
          g_free (s);
          gdm_fail (_("%s: XDMCP disabled and no static servers defined. Aborting!"), "gdm_config_parse");
       }
    }

    /* If no displays were found, then obviously
       we're in a no console mode */
    if (displays == NULL)
       no_console = TRUE;

    if (no_console)
       GdmConsoleNotify = FALSE;

    /* Lookup user and groupid for the GDM user */
    pwent = getpwnam (GdmUser);

    /* Set GdmUserId and GdmGroupId */
    if G_UNLIKELY (pwent == NULL) {
       gchar *s = g_strdup_printf (C_(N_("The GDM user '%s' does not exist. "
                                        "Please correct GDM configuration "
                                        "and restart GDM.")), GdmUser);
       if (GdmConsoleNotify)
          gdm_text_message_dialog (s);

       GdmPidFile = NULL;
       g_free (s);
       gdm_fail (_("%s: Can't find the GDM user '%s'. Aborting!"), "gdm_config_parse", GdmUser);
    } else {
       GdmUserId = pwent->pw_uid;
    }

    if G_UNLIKELY (GdmUserId == 0) {
       gchar *s = g_strdup_printf (C_(N_("The GDM user is set to be root, but "
                                        "this is not allowed since it can "
                                        "pose a security risk.  Please "
                                        "correct GDM configuration and "
                                        "restart GDM.")));
       if (GdmConsoleNotify)
          gdm_text_message_dialog (s);
       GdmPidFile = NULL;
       g_free (s);
       gdm_fail (_("%s: The GDM user should not be root. Aborting!"), "gdm_config_parse");
    }

    grent = getgrnam (GdmGroup);

    if G_UNLIKELY (grent == NULL) {
       gchar *s = g_strdup_printf (C_(N_("The GDM group '%s' does not exist. "
                                        "Please correct GDM configuration "
                                        "and restart GDM.")), GdmGroup);
       if (GdmConsoleNotify)
          gdm_text_message_dialog (s);
       GdmPidFile = NULL;
       g_free (s);
       gdm_fail (_("%s: Can't find the GDM group '%s'. Aborting!"), "gdm_config_parse", GdmGroup);
    } else  {
       GdmGroupId = grent->gr_gid;   
    }

    if G_UNLIKELY (GdmGroupId == 0) {
       gchar *s = g_strdup_printf (C_(N_("The GDM group is set to be root, but "
                                        "this is not allowed since it can "
                                        "pose a security risk. Please "
                                        "correct GDM configuration and "
                                        "restart GDM.")));
       if (GdmConsoleNotify)
          gdm_text_message_dialog (s);
       GdmPidFile = NULL;
       g_free (s);
       gdm_fail (_("%s: The GDM group should not be root. Aborting!"), "gdm_config_parse");
    }

    /* gid remains `gdm' */
    NEVER_FAILS_root_set_euid_egid (GdmUserId, GdmGroupId);

    /* Check that the greeter can be executed */
    bin = ve_first_word (GdmGreeter);
    if G_UNLIKELY (ve_string_empty (bin) || access (bin, X_OK) != 0) {
       gdm_error (_("%s: Greeter not found or can't be executed by the GDM user"), "gdm_config_parse");
    }
    g_free (bin);

    bin = ve_first_word (GdmRemoteGreeter);
    if G_UNLIKELY (ve_string_empty (bin) || access (bin, X_OK) != 0) {
       gdm_error (_("%s: Remote greeter not found or can't be executed by the GDM user"), "gdm_config_parse");
    }
    g_free (bin);

    /* Check that chooser can be executed */
    bin = ve_first_word (GdmChooser);

    if G_UNLIKELY (GdmIndirect && (ve_string_empty (bin) || access (bin, X_OK) != 0)) {
       gdm_error (_("%s: Chooser not found or it can't be executed by the GDM user"), "gdm_config_parse");
    }
    
    g_free (bin);

    /* Check the serv auth and log dirs */
    if G_UNLIKELY (ve_string_empty (GdmServAuthDir)) {
        if (GdmConsoleNotify)
           gdm_text_message_dialog
              (C_(N_("No daemon/ServAuthDir specified in the GDM configuration file")));
        GdmPidFile = NULL;
        gdm_fail (_("%s: No daemon/ServAuthDir specified."), "gdm_config_parse");
    }

    if (ve_string_empty (GdmLogDir)) {
       g_free (GdmLogDir);
       GdmLogDir = g_strdup (GdmServAuthDir);
    }

    /* Enter paranoia mode */
    check_servauthdir (&statbuf);

    NEVER_FAILS_root_set_euid_egid (0, 0);

    /* Now set things up for us as  */
    chown (GdmServAuthDir, 0, GdmGroupId);
    chmod (GdmServAuthDir, (S_IRWXU|S_IRWXG|S_ISVTX));

    NEVER_FAILS_root_set_euid_egid (GdmUserId, GdmGroupId);

    /* again paranoid */
    check_servauthdir (&statbuf);

    if G_UNLIKELY (statbuf.st_uid != 0 || statbuf.st_gid != GdmGroupId)  {
       gchar *s = g_strdup_printf (C_(N_("Server Authorization directory "
                                        "(daemon/ServAuthDir) is set to %s "
                                        "but is not owned by user %s and group "
                                        "%s. Please correct the ownership or "
                                        "GDM configuration and restart "
                                        "GDM.")), GdmServAuthDir, GdmUser, GdmGroup);
        if (GdmConsoleNotify)
           gdm_text_message_dialog (s);
           GdmPidFile = NULL;
           g_free (s);
           gdm_fail (_("%s: Authdir %s is not owned by user %s, group %s. Aborting."),
              "gdm_config_parse", GdmServAuthDir, gdm_root_user (), GdmGroup);
    }

    if G_UNLIKELY (statbuf.st_mode != (S_IFDIR|S_IRWXU|S_IRWXG|S_ISVTX))  {
       gchar *s = g_strdup_printf (C_(N_("Server Authorization directory "
                                 "(daemon/ServAuthDir) is set to %s "
                                 "but has the wrong permissions: it "
                                 "should have permissions of %o. "
                                 "Please correct the permissions or "
                                 "the GDM configuration and "
                                 "restart GDM.")), GdmServAuthDir,
                                 (S_IRWXU|S_IRWXG|S_ISVTX));
       if (GdmConsoleNotify)
          gdm_text_message_dialog (s);
       GdmPidFile = NULL;
       g_free (s);
       gdm_fail (_("%s: Authdir %s has wrong permissions %o. Should be %o. Aborting."), "gdm_config_parse", 
                GdmServAuthDir, statbuf.st_mode, (S_IRWXU|S_IRWXG|S_ISVTX));
    }

    NEVER_FAILS_root_set_euid_egid (0, 0);

    check_logdir ();

    /* Check that user authentication is properly configured */
    gdm_verify_check ();

    ve_config_destroy (cfg);
}

/** 
 * gdm_get_gdmuid
 * gdm_get_gdmgid
 *
 * Access functions for getting the GDM user ID and group ID.
 */
uid_t
gdm_get_gdmuid (void)
{
   return GdmUserId;
}

gid_t
gdm_get_gdmgid (void)
{
   return GdmGroupId;
}

/** 
 * gdm_get_high_display_num
 * gdm_get_high_display_num
 *
 * Access functions for getting the high display number.
 */
gint
gdm_get_high_display_num (void)
{
   return high_display_num;
}

void
gdm_set_high_display_num (gint val)
{
   high_display_num = val;
}

/**
 * gdm_print_config_option
 *
 * Called by gdm_print_all_config in a loop to print each key.
 * 
 */
void
gdm_print_config_option (gpointer key_in, gpointer value_in, gpointer data)
{
   gchar *key = (gchar *)key_in;
   gchar *retval;
   GdmConfigType *type = (GdmConfigType *)value_in;

   gdm_config_to_string (key, &retval);
   if (retval != NULL) {
      gdm_debug ("key is %s, value is %s\n", key, retval);
      g_free (retval);
   } else
      gdm_debug ("key is %s, value is <NULL>\n", key);
}

/**
 * gdm_print_all_config
 *
 * Not used by GDM, but useful for debug purposes.
 */
void
gdm_print_all_config (void)
{
   gdm_config_parse();

   g_hash_table_foreach (type_hash, gdm_print_config_option, NULL);
}

/**
 * gdm_signal_terminthup_was_notified
 *
 * returns TRUE if signal SIGTERM, SIGINT, or SIGHUP was received.
 * This just hides these vicious-extensions functions from the
 * other files
 */
gboolean
gdm_signal_terminthup_was_notified (void)
{
   if (ve_signal_was_notified (SIGTERM) ||
       ve_signal_was_notified (SIGINT) ||
       ve_signal_was_notified (SIGHUP)) {
      return TRUE;
   } else {
      return FALSE;
   }
}

/**
 * check_user_file
 * check_global_file
 * is_in_trusted_pic_dir
 * get_facefile_from_gnome2_dir_config 
 * path_is_local
 * gdm_get_facefile_from_home
 * gdm_get_facefile_from_global
 *
 * These functions are used for accessing the user's face image from their
 * home directory via vicious-extensions.
 */
static gboolean
check_user_file (const char *path,
                 guint       uid)
{
        char    *dir;
        char    *file;
        gboolean is_ok;

        if (path == NULL)
                return FALSE;

        if (access (path, R_OK) != 0)
                return FALSE;

        dir = g_path_get_dirname (path);
        file = g_path_get_basename (path);

        is_ok = gdm_file_check ("run_pictures",
                                uid,
                                dir,
                                file,
                                TRUE, TRUE,
                                gdm_get_value_int (GDM_KEY_USER_MAX_FILE),
                                gdm_get_value_int (GDM_KEY_RELAX_PERM));
        g_free (dir);
        g_free (file);

        return is_ok;
}

static gboolean
check_global_file (const char *path,
                   guint       uid)
{
        if (path == NULL)
                return FALSE;

        if (access (path, R_OK) != 0)
                return FALSE;

        return TRUE;
}

/* If path starts with a "trusted" directory, don't sanity check things */
/* This is really somewhat "outdated" as we now really want things in
 * the picture dir or in ~/.gnome2/photo */
static gboolean
is_in_trusted_pic_dir (const char *path)
{
        /* our own pixmap dir is trusted */
        if (strncmp (path, EXPANDED_PIXMAPDIR, sizeof (EXPANDED_PIXMAPDIR)) == 0)
                return TRUE;

        return FALSE;
}

static gchar *
get_facefile_from_gnome2_dir_config (const char *homedir,
                                     guint       uid)
{
   char *picfile = NULL;
   char *cfgdir;

   /* Sanity check on ~user/.gnome2/gdm */
   cfgdir = g_build_filename (homedir, ".gnome2", "gdm", NULL);
   if (G_LIKELY (check_user_file (cfgdir, uid))) {
      VeConfig *cfg;
      char *cfgfile;

      cfgfile = g_build_filename (homedir, ".gnome2", "gdm", NULL);
      cfg = ve_config_new (cfgfile);
      g_free (cfgfile);

      picfile = ve_config_get_string (cfg, "face/picture=");
      ve_config_destroy (cfg);

      /* must exist and be absolute (note that this check
       * catches empty strings)*/
      /* Note that these days we just set ~/.face */
      if G_UNLIKELY (picfile != NULL &&
                     (picfile[0] != '/' ||
                      /* this catches readability by user */
                      access (picfile, R_OK) != 0)) {
         g_free (picfile);
         picfile = NULL;
      }

      if (picfile != NULL) {
         char buf[PATH_MAX];
         if (realpath (picfile, buf) == NULL) {
            g_free (picfile);
            picfile = NULL;
         } else {
            g_free (picfile);
            picfile = g_strdup (buf);
         }
      }

      if G_UNLIKELY (picfile != NULL) {
         if (! is_in_trusted_pic_dir (picfile)) {
            /* if not in trusted dir, check it out */

            /* Note that strict permissions checking is done
             * on this file.  Even if it may not even be owned by the
             * user.  This setting should ONLY point to pics in trusted
             * dirs. */
            if (! check_user_file (picfile, uid)) {
               g_free (picfile);
               picfile = NULL;
            }
         }
      }
   }
   g_free (cfgdir);

   return picfile;
}

static GHashTable *fstype_hash = NULL;
extern char *filesystem_type (char *path, char *relpath, struct stat *statp);

static gboolean
path_is_local (const char *path)
{
        gpointer local = NULL;

        if (path == NULL)
                return FALSE;

        if (fstype_hash == NULL)
                fstype_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
        else
                local = g_hash_table_lookup (fstype_hash, path);

        if (local == NULL) {
                struct stat statbuf;

                if (stat (path, &statbuf) == 0) {
                        char *type = filesystem_type ((char *)path, (char *)path, &statbuf);
                        gboolean is_local = ((strcmp (type, "nfs") != 0) &&
                                             (strcmp (type, "afs") != 0) &&
                                             (strcmp (type, "autofs") != 0) &&
                                             (strcmp (type, "unknown") != 0) &&
                                             (strcmp (type, "ncpfs") != 0));
                        local = GINT_TO_POINTER (is_local ? 1 : -1);
                        g_hash_table_insert (fstype_hash, g_strdup (path), local);
                }
        }

        return GPOINTER_TO_INT (local) > 0;
}

gchar *
gdm_get_facefile_from_home (const char *homedir,
                            guint       uid)
{
   char    *picfile = NULL;
   char    *path;
   gboolean is_local;

   /* special case: look at parent of home to detect autofs
      this is so we don't try to trigger an automount */
   path = g_path_get_dirname (homedir);
   is_local = path_is_local (path);
   g_free (path);

   /* now check that home dir itself is local */
   if (is_local) {
      is_local = path_is_local (homedir);
   }

   /* only look at local home directories so we don't try to
      read from remote (e.g. NFS) volumes */
   if (! is_local)
      return NULL;


   picfile = g_build_filename (homedir, ".face", NULL);

   if (check_user_file (picfile, uid))
      return picfile;
   else {
      g_free (picfile);
      picfile = NULL;
   }

   picfile = g_build_filename (homedir, ".face.icon", NULL);

   if (check_user_file (picfile, uid))
           return picfile;
   else {
      g_free (picfile);
      picfile = NULL;
   }

   picfile = get_facefile_from_gnome2_dir_config (homedir, uid);
   if (check_user_file (picfile, uid))
      return picfile;
   else {
      g_free (picfile);
      picfile = NULL;
   }

   /* Nothing found yet, try the old locations */

   picfile = g_build_filename (homedir, ".gnome2", "photo", NULL);
   if (check_user_file (picfile, uid))
      return picfile;
   else {
      g_free (picfile);
      picfile = NULL;
   }

   picfile = g_build_filename (homedir, ".gnome", "photo", NULL);
   if (check_user_file (picfile, uid))
      return picfile;
   else {
      g_free (picfile);
      picfile = NULL;
   }

   return NULL;
}

gchar *
gdm_get_facefile_from_global (const char *username,
                              guint       uid)
{
   char *picfile = NULL;
   char *facedir = gdm_get_value_string (GDM_KEY_FACE_DIR);

   /* Try the global face directory */

   picfile = g_build_filename (facedir, username, NULL);

   if (check_global_file (picfile, uid))
      return picfile;

   g_free (picfile);
   picfile = gdm_make_filename (facedir, username, ".png");

   if (check_global_file (picfile, uid))
      return picfile;

   g_free (picfile);
   return NULL;
}

/**
 * gdm_get_session_exec
 *
 * This function accesses the GDM session desktop file, via vicious
 * extensions and returns the execution command for starting the
 * session.
 */
char *
gdm_get_session_exec (const char *session_name, gboolean check_try_exec)
{
   char *file;
   char *full = NULL;
   VeConfig *cfg;
   static char *exec;
   static char *cached = NULL;
   char *tryexec;

   /* clear cache */
   if (session_name == NULL) {
      g_free (exec);
      exec = NULL;
      g_free (cached);
      cached = NULL;
      return NULL;
   }

   if (cached != NULL && strcmp (session_name, cached) == 0)
      return g_strdup (exec);

   g_free (exec);
   exec = NULL;
   g_free (cached);
   cached = g_strdup (session_name);

   /* Some ugly special casing for legacy "Default.desktop", oh well,
    * we changed to "default.desktop" */
   if (g_ascii_strcasecmp (session_name, "default") == 0 ||
       g_ascii_strcasecmp (session_name, "default.desktop") == 0) {
      full = ve_find_prog_in_path ("default.desktop",
         gdm_get_value_string (GDM_KEY_SESSION_DESKTOP_DIR));
   }

   if (full == NULL) {
      file = gdm_ensure_extension (session_name, ".desktop");
      full = ve_find_prog_in_path (file,
         gdm_get_value_string (GDM_KEY_SESSION_DESKTOP_DIR));
      g_free (file);
   }

   if (ve_string_empty (full) || access (full, R_OK) != 0) {
      g_free (full);
      if (gdm_is_session_magic (session_name)) {
         exec = g_strdup (session_name);
         return g_strdup (exec);
      } else {
         return NULL;
      }
   }

   cfg = ve_config_get (full);
   g_free (full);
   if (ve_config_get_bool (cfg, "Desktop Entry/Hidden=false"))
      return NULL;

   if (check_try_exec) {
      tryexec = ve_config_get_string (cfg, "Desktop Entry/TryExec");
      if ( ! ve_string_empty (tryexec) &&
           ! ve_is_prog_in_path (tryexec, gdm_get_value_string (GDM_KEY_PATH)) &&
           ! ve_is_prog_in_path (tryexec, gdm_saved_getenv ("PATH"))) {
         g_free (tryexec);
         return NULL;
      }
      g_free (tryexec);
   }

   exec = ve_config_get_string (cfg, "Desktop Entry/Exec");
   return g_strdup (exec);
}

/**
 * gdm_set_user_session_lang
 * gdm_get_user_session_lang
 *
 * These functions get and set the user's language and setting in their
 * $HOME/.dmrc file via vicious-extensions.
 */
void
gdm_set_user_session_lang (gboolean savesess, gboolean savelang,
    const char *home_dir, const char *save_session, const char *save_language)
{
   VeConfig *dmrc = NULL;
   gchar *cfgstr = g_build_filename (home_dir, ".dmrc", NULL);

   if (savesess) {
      dmrc = ve_config_new (cfgstr);
      ve_config_set_string (dmrc, "Desktop/Session",
         ve_sure_string (save_session));
   }

   if (savelang) {
      if (dmrc == NULL)
         dmrc = ve_config_new (cfgstr);
      if (ve_string_empty (save_language))
         /* we chose the system default language so wipe the
          * lang key */
         ve_config_delete_key (dmrc, "Desktop/Language");
      else
         ve_config_set_string (dmrc, "Desktop/Language", save_language);
   }

   g_free (cfgstr);

   if (dmrc != NULL) {
      mode_t oldmode;
      oldmode = umask (077);
      ve_config_save (dmrc, FALSE);
      ve_config_destroy (dmrc);
      dmrc = NULL;
      umask (oldmode);
   }
}

void
gdm_get_user_session_lang (char **usrsess, char **usrlang,
   const char *home_dir, gboolean *savesess)
{
   char *p;
   char *cfgfile = g_build_filename (home_dir, ".dmrc", NULL);
   VeConfig *cfg = ve_config_new (cfgfile);
   g_free (cfgfile);

   *usrsess = ve_config_get_string (cfg, "Desktop/Session");
   if (*usrsess == NULL)
      *usrsess = g_strdup ("");

   /* this is just being truly anal about what users give us, and in case
    * it looks like they may have included a path whack it. */
   p = strrchr (*usrsess, '/');
   if (p != NULL) {
      char *tmp = g_strdup (p+1);
      g_free (*usrsess);
      *usrsess = tmp;
   }

   /* ugly workaround for migration */
   if ((strcmp (*usrsess, "Default.desktop") == 0 ||
        strcmp (*usrsess, "Default") == 0) &&
       ! ve_is_prog_in_path ("Default.desktop",
            gdm_get_value_string (GDM_KEY_SESSION_DESKTOP_DIR))) {
           g_free (*usrsess);
           *usrsess = g_strdup ("default");
           *savesess = TRUE;
   }

   *usrlang = ve_config_get_string (cfg, "Desktop/Language");
   if (*usrlang == NULL)
           *usrlang = g_strdup ("");

   ve_config_destroy (cfg);
}

