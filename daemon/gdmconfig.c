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
#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "vicious.h"

#include "gdm.h"
#include "gdmconfig.h"
#include "verify.h"
#include "gdm-net.h"
#include "misc.h"
#include "server.h"
#include "filecheck.h"
#include "slave.h"

gchar *config_file                     = NULL;
gchar *custom_config_file              = NULL;
static time_t config_file_mtime        = 0;
static time_t custom_config_file_mtime = 0;

extern gboolean no_console;
extern gboolean gdm_emergency_server;

GSList *displays          = NULL;
GSList *displays_inactive = NULL;
GSList *xservers          = NULL;

gint high_display_num = 0;

typedef enum {
        CONFIG_BOOL,
        CONFIG_INT,
        CONFIG_STRING
} GdmConfigType;

static GHashTable    *type_hash       = NULL;
static GHashTable    *val_hash        = NULL;
static GHashTable    *translated_hash = NULL;
static GHashTable    *realkey_hash    = NULL;
static GdmConfigType  bool_type       = CONFIG_BOOL;
static GdmConfigType  int_type        = CONFIG_INT;
static GdmConfigType  string_type     = CONFIG_STRING;

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
static gchar *GdmDisplayInitDir = NULL;
static gchar *GdmPostLogin = NULL;
static gchar *GdmPreSession = NULL;
static gchar *GdmPostSession = NULL;
static gchar *GdmFailsafeXserver = NULL;
static gchar *GdmXKeepsCrashing = NULL;
static gchar *GdmHalt = NULL;
static gchar *GdmReboot = NULL;
static gchar *GdmSuspend = NULL;
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
static gchar *GdmSoundOnLoginFile = NULL;
static gchar *GdmSoundOnLoginSuccessFile = NULL;
static gchar *GdmSoundOnLoginFailureFile = NULL;
static gchar *GdmConsoleCannotHandle = NULL;
static gchar *GdmPamStack = NULL;

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
static gint GdmXserverTimeout = 10;

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
static gboolean GdmSoundOnLogin = TRUE;
static gboolean GdmSoundOnLoginSuccess = FALSE;
static gboolean GdmSoundOnLoginFailure = FALSE;
static gboolean GdmConsoleNotify = TRUE;

/* Config options used by slave */
/* ---------------------------- */
static gchar *GdmGtkThemesToAllow = NULL;
static gchar *GdmInclude = NULL;
static gchar *GdmExclude = NULL;
static gchar *GdmDefaultFace = NULL;
static gchar *GdmLocaleFile = NULL;
static gchar *GdmLogo = NULL;
static gchar *GdmChooserButtonLogo = NULL;
static gchar *GdmWelcome = NULL;
static gchar *GdmRemoteWelcome = NULL;
static gchar *GdmBackgroundProgram = NULL;
static gchar *GdmBackgroundImage = NULL;
static gchar *GdmBackgroundColor = NULL;
static gchar *GdmGraphicalTheme = NULL;
static gchar *GdmInfoMsgFile = NULL;
static gchar *GdmInfoMsgFont = NULL;
static gchar *GdmHost = NULL;
static gchar *GdmHostImageDir = NULL;
static gchar *GdmHosts = NULL;
static gchar *GdmGraphicalThemeColor = NULL;
static gchar *GdmGraphicalThemeDir = NULL;
static gchar *GdmGraphicalThemes = NULL;
static gchar *GdmPreFetchProgram = NULL;
static gchar *GdmUse24Clock = NULL;

static gint GdmPositionX;
static gint GdmPositionY;
static gint GdmMinimalUid;
static gint GdmMaxIconWidth;
static gint GdmMaxIconHeight;
static gint GdmBackgroundType;
static gint GdmScanTime;
static gint GdmMaxWait;
static gint GdmFlexiReapDelayMinutes;
static gint GdmBackgroundProgramInitialDelay = 30;
static gint GdmBackgroundProgramRestartDelay = 30;

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
static gboolean GdmEntryCircles;
static gboolean GdmEntryInvisible;
static gboolean GdmGraphicalThemeRand;
static gboolean GdmBroadcast;
static gboolean GdmAllowAdd;
static gboolean GdmRestartBackgroundProgram;

/**
 * gdm_config_add_hash
 *
 * Add config value to the val_hash and type_hash.  Strip the key so
 * it doesn't contain a default value.  This function assumes the
 * val_hash, type_hash, and realkey_hash have been initialized.
 */
static void
gdm_config_add_hash (gchar *key, gpointer value, GdmConfigType *type)
{
   gchar *p;
   gchar *newkey = g_strdup (key);

   g_strstrip (newkey);
   p = strchr (newkey, '=');
   if (p != NULL)
      *p = '\0';

   g_hash_table_insert (val_hash, newkey, value);
   g_hash_table_insert (type_hash, newkey, type);
   g_hash_table_insert (realkey_hash, newkey, key);
}

/**
 * gdm_config_hash_lookup
 *
 * Accesses hash with key, stripping it so it doesn't contain a default
 * value.
 */
static gpointer
gdm_config_hash_lookup (GHashTable *hash, gchar *key)
{
   gchar *p;
   gpointer *ret;
   gchar *newkey = g_strdup (key);

   g_strstrip (newkey);
   p = strchr (newkey, '=');
   if (p != NULL)
      *p = '\0';

   ret = g_hash_table_lookup (hash, newkey);
   g_free (newkey);
   return (ret);
}

/**
 * is_key
 *
 * Since GDM keys sometimes have default values defined in the gdm.h header
 * file (e.g. key=value), this function strips off the "=value" from both 
 * keys passed and compares them, returning TRUE if they are the same, 
 * FALSE otherwise.
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

   if (strcmp (ve_sure_string (key1d), ve_sure_string (key2d)) == 0) {
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
 * gdm_config_init
 * 
 * Sets up initial hashes used by configuration routines.
 */ 
static void 
gdm_config_init (void)
{
   type_hash    = g_hash_table_new (g_str_hash, g_str_equal);
   val_hash     = g_hash_table_new (g_str_hash, g_str_equal);
   realkey_hash = g_hash_table_new (g_str_hash, g_str_equal);

   /* boolean values */
   gdm_config_add_hash (GDM_KEY_ALLOW_REMOTE_ROOT, &GdmAllowRemoteRoot, &bool_type);
   gdm_config_add_hash (GDM_KEY_ALLOW_ROOT, &GdmAllowRoot, &bool_type);
   gdm_config_add_hash (GDM_KEY_ALLOW_REMOTE_AUTOLOGIN,
      &GdmAllowRemoteAutoLogin, &bool_type);
   gdm_config_add_hash (GDM_KEY_PASSWORD_REQUIRED, &GdmPasswordRequired, &bool_type);
   gdm_config_add_hash (GDM_KEY_AUTOMATIC_LOGIN_ENABLE,
      &GdmAutomaticLoginEnable, &bool_type);
   gdm_config_add_hash (GDM_KEY_ALWAYS_RESTART_SERVER,
      &GdmAlwaysRestartServer, &bool_type);
   gdm_config_add_hash (GDM_KEY_ADD_GTK_MODULES, &GdmAddGtkModules, &bool_type);
   gdm_config_add_hash (GDM_KEY_DOUBLE_LOGIN_WARNING,
      &GdmDoubleLoginWarning, &bool_type);
   gdm_config_add_hash (GDM_KEY_ALWAYS_LOGIN_CURRENT_SESSION,
      &GdmAlwaysLoginCurrentSession, &bool_type);
   gdm_config_add_hash (GDM_KEY_DISPLAY_LAST_LOGIN, &GdmDisplayLastLogin, &bool_type);
   gdm_config_add_hash (GDM_KEY_KILL_INIT_CLIENTS, &GdmKillInitClients, &bool_type);
   gdm_config_add_hash (GDM_KEY_CONFIG_AVAILABLE, &GdmConfigAvailable, &bool_type);
   gdm_config_add_hash (GDM_KEY_SYSTEM_MENU, &GdmSystemMenu, &bool_type);
   gdm_config_add_hash (GDM_KEY_CHOOSER_BUTTON, &GdmChooserButton, &bool_type);
   gdm_config_add_hash (GDM_KEY_BROWSER, &GdmBrowser, &bool_type);
   gdm_config_add_hash (GDM_KEY_MULTICAST, &GdmMulticast, &bool_type);
   gdm_config_add_hash (GDM_KEY_NEVER_PLACE_COOKIES_ON_NFS,
      &GdmNeverPlaceCookiesOnNfs, &bool_type);
   gdm_config_add_hash (GDM_KEY_CONSOLE_NOTIFY, &GdmConsoleNotify, &bool_type);
   gdm_config_add_hash (GDM_KEY_TIMED_LOGIN_ENABLE, &GdmTimedLoginEnable, &bool_type);
   gdm_config_add_hash (GDM_KEY_CHECK_DIR_OWNER, &GdmCheckDirOwner, &bool_type);
   gdm_config_add_hash (GDM_KEY_XDMCP, &GdmXdmcp, &bool_type);
   gdm_config_add_hash (GDM_KEY_INDIRECT, &GdmIndirect, &bool_type);
   gdm_config_add_hash (GDM_KEY_XDMCP_PROXY, &GdmXdmcpProxy, &bool_type);
   gdm_config_add_hash (GDM_KEY_DYNAMIC_XSERVERS, &GdmDynamicXservers, &bool_type);
   gdm_config_add_hash (GDM_KEY_VT_ALLOCATION, &GdmVTAllocation, &bool_type);
   gdm_config_add_hash (GDM_KEY_DISALLOW_TCP, &GdmDisallowTcp, &bool_type);
   gdm_config_add_hash (GDM_KEY_SOUND_ON_LOGIN_SUCCESS,
      &GdmSoundOnLoginSuccess, &bool_type);
   gdm_config_add_hash (GDM_KEY_SOUND_ON_LOGIN_FAILURE,
      &GdmSoundOnLoginFailure, &bool_type);
   gdm_config_add_hash (GDM_KEY_DEBUG, &GdmDebug, &bool_type);
   gdm_config_add_hash (GDM_KEY_DEBUG_GESTURES, &GdmDebugGestures, &bool_type);
   gdm_config_add_hash (GDM_KEY_ALLOW_GTK_THEME_CHANGE,
      &GdmAllowGtkThemeChange, &bool_type);
   gdm_config_add_hash (GDM_KEY_TITLE_BAR, &GdmTitleBar, &bool_type);
   gdm_config_add_hash (GDM_KEY_INCLUDE_ALL, &GdmIncludeAll, &bool_type);
   gdm_config_add_hash (GDM_KEY_DEFAULT_WELCOME, &GdmDefaultWelcome, &bool_type);
   gdm_config_add_hash (GDM_KEY_DEFAULT_REMOTE_WELCOME,
      &GdmDefaultRemoteWelcome, &bool_type);
   gdm_config_add_hash (GDM_KEY_LOCK_POSITION, &GdmLockPosition, &bool_type);
   gdm_config_add_hash (GDM_KEY_BACKGROUND_SCALE_TO_FIT,
      &GdmBackgroundScaleToFit, &bool_type);
   gdm_config_add_hash (GDM_KEY_BACKGROUND_REMOTE_ONLY_COLOR,
      &GdmBackgroundRemoteOnlyColor, &bool_type);
   gdm_config_add_hash (GDM_KEY_RUN_BACKGROUND_PROGRAM_ALWAYS,
      &GdmRunBackgroundProgramAlways, &bool_type);
   gdm_config_add_hash (GDM_KEY_SET_POSITION, &GdmSetPosition, &bool_type);
   gdm_config_add_hash (GDM_KEY_QUIVER, &GdmQuiver, &bool_type);
   gdm_config_add_hash (GDM_KEY_SHOW_GNOME_FAILSAFE,
      &GdmShowGnomeFailsafe, &bool_type);
   gdm_config_add_hash (GDM_KEY_SHOW_XTERM_FAILSAFE,
      &GdmShowXtermFailsafe, &bool_type);
   gdm_config_add_hash (GDM_KEY_SHOW_LAST_SESSION,
      &GdmShowLastSession, &bool_type);
   gdm_config_add_hash (GDM_KEY_USE_24_CLOCK, &GdmUse24Clock, &string_type);
   gdm_config_add_hash (GDM_KEY_ENTRY_CIRCLES, &GdmEntryCircles, &bool_type);
   gdm_config_add_hash (GDM_KEY_ENTRY_INVISIBLE, &GdmEntryInvisible, &bool_type);
   gdm_config_add_hash (GDM_KEY_GRAPHICAL_THEME_RAND,
      &GdmGraphicalThemeRand, &bool_type);
   gdm_config_add_hash (GDM_KEY_BROADCAST, &GdmBroadcast, &bool_type);
   gdm_config_add_hash (GDM_KEY_ALLOW_ADD, &GdmAllowAdd, &bool_type);
   gdm_config_add_hash (GDM_KEY_SOUND_ON_LOGIN, &GdmSoundOnLogin, &bool_type);
   gdm_config_add_hash (GDM_KEY_RESTART_BACKGROUND_PROGRAM,
      &GdmRestartBackgroundProgram, &bool_type);

   /* string values */
   gdm_config_add_hash (GDM_KEY_PATH, &GdmPath, &string_type);
   gdm_config_add_hash (GDM_KEY_ROOT_PATH, &GdmRootPath, &string_type);
   gdm_config_add_hash (GDM_KEY_CONSOLE_CANNOT_HANDLE,
      &GdmConsoleCannotHandle, &string_type);
   gdm_config_add_hash (GDM_KEY_CHOOSER, &GdmChooser, &string_type);
   gdm_config_add_hash (GDM_KEY_GREETER, &GdmGreeter, &string_type);
   gdm_config_add_hash (GDM_KEY_CONFIGURATOR, &GdmConfigurator, &string_type);
   gdm_config_add_hash (GDM_KEY_POSTLOGIN, &GdmPostLogin, &string_type);
   gdm_config_add_hash (GDM_KEY_PRESESSION, &GdmPreSession, &string_type);
   gdm_config_add_hash (GDM_KEY_POSTSESSION, &GdmPostSession, &string_type);
   gdm_config_add_hash (GDM_KEY_FAILSAFE_XSERVER, &GdmFailsafeXserver, &string_type);
   gdm_config_add_hash (GDM_KEY_X_KEEPS_CRASHING, &GdmXKeepsCrashing, &string_type);
   gdm_config_add_hash (GDM_KEY_BASE_XSESSION, &GdmBaseXsession, &string_type);
   gdm_config_add_hash (GDM_KEY_REMOTE_GREETER, &GdmRemoteGreeter, &string_type);
   gdm_config_add_hash (GDM_KEY_DISPLAY_INIT_DIR, &GdmDisplayInitDir, &string_type);
   gdm_config_add_hash (GDM_KEY_AUTOMATIC_LOGIN, &GdmAutomaticLogin, &string_type);
   gdm_config_add_hash (GDM_KEY_GTK_MODULES_LIST, &GdmGtkModulesList, &string_type);
   gdm_config_add_hash (GDM_KEY_REBOOT, &GdmReboot, &string_type);
   gdm_config_add_hash (GDM_KEY_HALT, &GdmHalt, &string_type);
   gdm_config_add_hash (GDM_KEY_SUSPEND, &GdmSuspend, &string_type);
   gdm_config_add_hash (GDM_KEY_LOG_DIR, &GdmLogDir, &string_type);
   gdm_config_add_hash (GDM_KEY_PID_FILE, &GdmPidFile, &string_type);
   gdm_config_add_hash (GDM_KEY_GLOBAL_FACE_DIR, &GdmGlobalFaceDir, &string_type);
   gdm_config_add_hash (GDM_KEY_SERV_AUTHDIR, &GdmServAuthDir, &string_type);
   gdm_config_add_hash (GDM_KEY_USER_AUTHDIR, &GdmUserAuthDir, &string_type);
   gdm_config_add_hash (GDM_KEY_USER_AUTHFILE, &GdmUserAuthFile, &string_type);
   gdm_config_add_hash (GDM_KEY_USER_AUTHDIR_FALLBACK,
      &GdmUserAuthFallback, &string_type);
   gdm_config_add_hash (GDM_KEY_SESSION_DESKTOP_DIR, &GdmSessDir, &string_type);
   gdm_config_add_hash (GDM_KEY_DEFAULT_SESSION, &GdmDefaultSession, &string_type);
   gdm_config_add_hash (GDM_KEY_MULTICAST_ADDR, &GdmMulticastAddr, &string_type);
   gdm_config_add_hash (GDM_KEY_USER, &GdmUser, &string_type);
   gdm_config_add_hash (GDM_KEY_GROUP, &GdmGroup, &string_type);
   gdm_config_add_hash (GDM_KEY_GTKRC, &GdmGtkRC, &string_type);
   gdm_config_add_hash (GDM_KEY_GTK_THEME, &GdmGtkTheme, &string_type);
   gdm_config_add_hash (GDM_KEY_TIMED_LOGIN, &GdmTimedLogin, &string_type);
   gdm_config_add_hash (GDM_KEY_WILLING, &GdmWilling, &string_type);
   gdm_config_add_hash (GDM_KEY_XDMCP_PROXY_XSERVER,
      &GdmXdmcpProxyXserver, &string_type);
   gdm_config_add_hash (GDM_KEY_XDMCP_PROXY_RECONNECT,
      &GdmXdmcpProxyReconnect, &string_type);
   gdm_config_add_hash (GDM_KEY_STANDARD_XSERVER, &GdmStandardXserver, &string_type);
   gdm_config_add_hash (GDM_KEY_XNEST, &GdmXnest, &string_type);
   gdm_config_add_hash (GDM_KEY_SOUND_PROGRAM, &GdmSoundProgram, &string_type);
   gdm_config_add_hash (GDM_KEY_SOUND_ON_LOGIN_FILE,
      &GdmSoundOnLoginFile, &string_type);
   gdm_config_add_hash (GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE,
      &GdmSoundOnLoginSuccessFile, &string_type);
   gdm_config_add_hash (GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE,
      &GdmSoundOnLoginFailureFile, &string_type);
   gdm_config_add_hash (GDM_KEY_GTK_THEMES_TO_ALLOW,
      &GdmGtkThemesToAllow, &string_type);
   gdm_config_add_hash (GDM_KEY_INCLUDE, &GdmInclude, &string_type);
   gdm_config_add_hash (GDM_KEY_EXCLUDE, &GdmExclude, &string_type);
   gdm_config_add_hash (GDM_KEY_DEFAULT_FACE, &GdmDefaultFace, &string_type);
   gdm_config_add_hash (GDM_KEY_LOCALE_FILE, &GdmLocaleFile, &string_type);
   gdm_config_add_hash (GDM_KEY_LOGO, &GdmLogo, &string_type);
   gdm_config_add_hash (GDM_KEY_CHOOSER_BUTTON_LOGO,
      &GdmChooserButtonLogo, &string_type);
   gdm_config_add_hash (GDM_KEY_WELCOME, &GdmWelcome, &string_type);
   gdm_config_add_hash (GDM_KEY_REMOTE_WELCOME, &GdmRemoteWelcome, &string_type);
   gdm_config_add_hash (GDM_KEY_BACKGROUND_PROGRAM,
      &GdmBackgroundProgram, &string_type);
   gdm_config_add_hash (GDM_KEY_BACKGROUND_IMAGE,
      &GdmBackgroundImage, &string_type);
   gdm_config_add_hash (GDM_KEY_BACKGROUND_COLOR,
      &GdmBackgroundColor, &string_type);
   gdm_config_add_hash (GDM_KEY_GRAPHICAL_THEME,
      &GdmGraphicalTheme, &string_type);
   gdm_config_add_hash (GDM_KEY_GRAPHICAL_THEME_DIR,
      &GdmGraphicalThemeDir, &string_type);
   gdm_config_add_hash (GDM_KEY_GRAPHICAL_THEMES,
      &GdmGraphicalThemes, &string_type);
   gdm_config_add_hash (GDM_KEY_GRAPHICAL_THEMED_COLOR,
      &GdmGraphicalThemeColor, &string_type);
   gdm_config_add_hash (GDM_KEY_INFO_MSG_FILE, &GdmInfoMsgFile, &string_type);
   gdm_config_add_hash (GDM_KEY_INFO_MSG_FONT, &GdmInfoMsgFont, &string_type);
   gdm_config_add_hash (GDM_KEY_DEFAULT_HOST_IMG, &GdmHost, &string_type);
   gdm_config_add_hash (GDM_KEY_HOST_IMAGE_DIR, &GdmHostImageDir, &string_type);
   gdm_config_add_hash (GDM_KEY_HOSTS, &GdmHosts, &string_type);
   gdm_config_add_hash (GDM_KEY_PRE_FETCH_PROGRAM,
      &GdmPreFetchProgram, &string_type);
   gdm_config_add_hash (GDM_KEY_PAM_STACK, &GdmPamStack, &string_type);

   /* int values */
   gdm_config_add_hash (GDM_KEY_XINERAMA_SCREEN, &GdmXineramaScreen, &int_type);
   gdm_config_add_hash (GDM_KEY_RETRY_DELAY, &GdmRetryDelay, &int_type);
   gdm_config_add_hash (GDM_KEY_TIMED_LOGIN_DELAY, &GdmTimedLoginDelay, &int_type);
   gdm_config_add_hash (GDM_KEY_RELAX_PERM, &GdmRelaxPerm, &int_type);
   gdm_config_add_hash (GDM_KEY_USER_MAX_FILE, &GdmUserMaxFile, &int_type);
   gdm_config_add_hash (GDM_KEY_DISPLAYS_PER_HOST, &GdmDisplaysPerHost, &int_type);
   gdm_config_add_hash (GDM_KEY_MAX_PENDING, &GdmMaxPending, &int_type);
   gdm_config_add_hash (GDM_KEY_MAX_WAIT, &GdmMaxWait, &int_type);
   gdm_config_add_hash (GDM_KEY_MAX_SESSIONS, &GdmMaxSessions, &int_type);
   gdm_config_add_hash (GDM_KEY_UDP_PORT, &GdmUdpPort, &int_type);
   gdm_config_add_hash (GDM_KEY_MAX_INDIRECT, &GdmMaxIndirect, &int_type);
   gdm_config_add_hash (GDM_KEY_MAX_WAIT_INDIRECT, &GdmMaxWaitIndirect, &int_type);
   gdm_config_add_hash (GDM_KEY_PING_INTERVAL, &GdmPingInterval, &int_type);
   gdm_config_add_hash (GDM_KEY_FLEXIBLE_XSERVERS, &GdmFlexibleXservers, &int_type);
   gdm_config_add_hash (GDM_KEY_FIRST_VT, &GdmFirstVt, &int_type);
   gdm_config_add_hash (GDM_KEY_POSITION_X, &GdmPositionX, &int_type);
   gdm_config_add_hash (GDM_KEY_POSITION_Y, &GdmPositionY, &int_type);
   gdm_config_add_hash (GDM_KEY_MINIMAL_UID, &GdmMinimalUid, &int_type);
   gdm_config_add_hash (GDM_KEY_MAX_ICON_WIDTH, &GdmMaxIconWidth, &int_type);
   gdm_config_add_hash (GDM_KEY_MAX_ICON_HEIGHT, &GdmMaxIconHeight, &int_type);
   gdm_config_add_hash (GDM_KEY_BACKGROUND_TYPE, &GdmBackgroundType, &int_type);
   gdm_config_add_hash (GDM_KEY_SCAN_TIME, &GdmScanTime, &int_type);
   gdm_config_add_hash (GDM_KEY_FLEXI_REAP_DELAY_MINUTES,
      &GdmFlexiReapDelayMinutes, &int_type);
   gdm_config_add_hash (GDM_KEY_BACKGROUND_PROGRAM_INITIAL_DELAY,
      &GdmBackgroundProgramInitialDelay, &int_type);
   gdm_config_add_hash (GDM_KEY_BACKGROUND_PROGRAM_RESTART_DELAY,
      &GdmBackgroundProgramRestartDelay, &int_type);
   gdm_config_add_hash (GDM_KEY_XSERVER_TIMEOUT, &GdmXserverTimeout, &int_type);
}

/**
 * gdm_get_config:
 *
 * Get config file.  
 */
static VeConfig *
gdm_get_default_config (struct stat *statbuf)
{
   int r;

   /* Not NULL if config_file was set by command-line option. */
   if (config_file != NULL) {
      VE_IGNORE_EINTR (r = g_stat (config_file, statbuf));
      if (r < 0) {
         gdm_error (_("%s: No GDM configuration file: %s. Using defaults."),
                      "gdm_config_parse", config_file);
         return NULL;
      }
   } else {
      VE_IGNORE_EINTR (r = g_stat (GDM_DEFAULTS_CONF, statbuf));
      if (r < 0) {
         gdm_error (_("%s: No GDM configuration file: %s. Using defaults."),
                      "gdm_config_parse", GDM_DEFAULTS_CONF);
         return NULL;
      } else {
              config_file = GDM_DEFAULTS_CONF;
      }
   }

   return ve_config_new (config_file);
}

/**
 * gdm_get_custom_config:
 *
 * Get the custom config file where gdmsetup saves its changes and
 * where users are encouraged to make modifications.
 */
static VeConfig *
gdm_get_custom_config (struct stat *statbuf)
{
   int r;

   /*
    * First check to see if the old configuration file name is on
    * the system.  If so, use that as the custom configuration
    * file.  "make install" will move this file aside, and 
    * distros probably can also manage moving this file on
    * upgrade.   
    *
    * In case this file is on the system, then use it as
    * the custom configuration file until the user moves it
    * aside.  This will likely mean all the defaults in
    * defaults.conf will not get used since the old gdm.conf
    * file has all the keys in it (except new ones).  But
    * that would be what the user wants.
    */
   VE_IGNORE_EINTR (r = g_stat (GDM_OLD_CONF, statbuf));
   if (r >= 0) {
      custom_config_file = g_strdup (GDM_OLD_CONF);
      return ve_config_new (custom_config_file);
   }

   VE_IGNORE_EINTR (r = g_stat (GDM_CUSTOM_CONF, statbuf));
   if (r >= 0) {
      custom_config_file = g_strdup (GDM_CUSTOM_CONF);
      return ve_config_new (custom_config_file);
   } else {
      return NULL;
   }
}

/**
 * gdm_get_per_display_custom_config_file
 *
 * Returns the per-display config file for a given display
 * This is always the custom config file name with the display
 * appended, and never gdm.conf.
 */
static gchar *
gdm_get_per_display_custom_config_file (gchar *display)
{
  return g_strdup_printf ("%s%s", custom_config_file, display);
}
 
/**
 * gdm_get_custom_config_file
 *
 * Returns the custom config file being used.
 */
gchar *
gdm_get_custom_config_file (void)
{
   return custom_config_file;
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
   GdmConfigType *type = gdm_config_hash_lookup (type_hash, key);
   gpointer val        = gdm_config_hash_lookup (val_hash, key);

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
   GdmConfigType *type;
   gpointer val;

   /* First look in translated_hash */
   if (translated_hash != NULL) {
      val = gdm_config_hash_lookup (translated_hash, key);
      if (val) {
         gchar **charval = (char **)val;
         return *charval;
      }
   }

   type = gdm_config_hash_lookup (type_hash, key);
   val  = gdm_config_hash_lookup (val_hash, key);

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
   GdmConfigType *type = gdm_config_hash_lookup (type_hash, key);
   gpointer val        = gdm_config_hash_lookup (val_hash, key);

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
 * Note that some GUI configuration parameters are read by the daemon,
 * and in order for them to work, it is necessary for the daemon to 
 * access a few keys in a per-display fashion.  These access functions
 * allow the daemon to access these keys properly.
 */

/**
 * gdm_get_value_int_per_display
 *
 * Gets the per-display version  of the configuration, or the default
 * value if none exists.
 */
gint gdm_get_value_int_per_display (gchar *display, gchar *key)
{
   gchar *perdispval;

   gdm_config_key_to_string_per_display (display, key, &perdispval);

   if (perdispval != NULL) {
      gint val = atoi (perdispval);
      g_free (perdispval);
      return val;
   } else {
      return (gdm_get_value_int (key));
   }
}
 
/**
 * gdm_get_value_bool_per_display
 *
 * Gets the per-display version  of the configuration, or the default
 * value if none exists.
 */
gboolean gdm_get_value_bool_per_display (gchar *display, gchar *key)
{
   gchar *perdispval;

   gdm_config_key_to_string_per_display (display, key, &perdispval);

   if (perdispval != NULL) {
      if (perdispval[0] == 'T' ||
          perdispval[0] == 't' ||
          perdispval[0] == 'Y' ||
          perdispval[0] == 'y' ||
          atoi (perdispval) != 0) {
		g_free (perdispval);
		return TRUE;
       } else {
		return FALSE;
       }
   } else {
      return (gdm_get_value_bool (key));
   }
}
 
/**
 * gdm_get_value_string_per_display
 *
 * Gets the per-display version  of the configuration, or the default
 * value if none exists.  Note that this value needs to be freed,
 * unlike the non-per-display version.
 */
gchar * gdm_get_value_string_per_display (gchar *display, gchar *key)
{
   gchar *perdispval;

   gdm_config_key_to_string_per_display (display, key, &perdispval);

   if (perdispval != NULL) {
      return perdispval;
   } else {
      return (g_strdup (gdm_get_value_string (key)));
   }
}

/**
 * gdm_config_key_to_string_per_display
 *
 * If the key makes sense to be per-display, return the value,
 * otherwise return NULL.  Keys that only apply to the daemon
 * process do not make sense for per-display configuration  
 * Valid keys include any key in the greeter or gui categories,
 * and the GDM_KEY_PAM_STACK key.
 *
 * If additional keys make sense for per-display usage, make
 * sure they are added to the if-test below.
 */
void
gdm_config_key_to_string_per_display (gchar *display, gchar *key, gchar **retval)
{
   gchar *file;
   gchar **splitstr = g_strsplit (key, "/", 2);
   *retval = NULL;

   if (display == NULL)
      return;

   file = gdm_get_per_display_custom_config_file (display);

   if (strcmp (ve_sure_string (splitstr[0]), "greeter") == 0 ||
       strcmp (ve_sure_string (splitstr[0]), "gui") == 0 ||
       is_key (key, GDM_KEY_PAM_STACK)) {
      gdm_config_key_to_string (file, key, retval);
   }

   g_free (file);

   return;
}

/**
 * gdm_config_key_to_string
 *
 * Returns a specific key from the config file, or NULL if not found.
 * Note this returns the value in string form, so the caller needs
 * to parse it properly if it is a bool or int.
 */
void
gdm_config_key_to_string (gchar *file, gchar *key, gchar **retval)
{
   VeConfig *cfg = ve_config_get (file);
   GdmConfigType *type = gdm_config_hash_lookup (type_hash, key);
   gchar **splitstr = g_strsplit (key, "/", 2);
   *retval = NULL;

   /* Should not fail, all keys should have a category. */
   if (splitstr[0] == NULL)
      return;

   /* If file doesn't exist, then just return */
   if (cfg == NULL)
      return;

   GList *list = ve_config_get_keys (cfg, splitstr[0]);
   while (list != NULL) {
      gchar *display_key     = (char *)list->data;
      gchar *display_fullkey = g_strdup_printf ("%s/%s", splitstr[0], display_key);

      if (is_key (key, display_fullkey)) {
         gdm_debug ("Returning value for key <%s>\n", key);
         if (*type == CONFIG_BOOL) {
            gboolean value = ve_config_get_bool (cfg, key);
            if (value)
               *retval = g_strdup ("true");
            else
               *retval = g_strdup ("false");
            return;
         } else if (*type == CONFIG_INT) {
            gint value = ve_config_get_int (cfg, key);
            *retval = g_strdup_printf ("%d", value);
            return;
         } else if (*type == CONFIG_STRING) {
            gchar *value = ve_config_get_string (cfg, key);
            if (value != NULL)
               *retval = g_strdup (value);
            return;
         }
      }
      g_free (display_fullkey);
      list = list->next;
   }
   return;
}

/**
 * gdm_config_to_string
 *
 * Returns a configuration option as a string.  Used by GDM's
 * GET_CONFIG socket command.
 */ 
void
gdm_config_to_string (gchar *key, gchar *display, gchar **retval)
{
   GdmConfigType *type = gdm_config_hash_lookup (type_hash, key);
   *retval = NULL;

   /*
    * See if there is a per-display config file, returning that value
    * if it exists.
    */
   if (display) {
      gdm_config_key_to_string_per_display (display, key, retval);
      if (*retval != NULL)
         return;
   }

   /* First look in translated_hash */
   if (translated_hash != NULL) {
      gpointer val = gdm_config_hash_lookup (translated_hash, key);
      if (val) {
         *retval = g_strdup (val);
         return;
      }
   }

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
 * gdm_compare_displays
 * 
 * Support function for loading displays from the configuration
 * file
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

static void
notify_displays_string (const gchar *key, const gchar *val)
{
   GSList *li;
   for (li = displays; li != NULL; li = li->next) {
      GdmDisplay *disp = li->data;
      if (disp->master_notify_fd >= 0) {
         if (val == NULL) {
            gdm_fdprintf (disp->master_notify_fd, "%c%s \n",
                          GDM_SLAVE_NOTIFY_KEY, key);
         } else {
            gdm_fdprintf (disp->master_notify_fd, "%c%s %s\n",
                          GDM_SLAVE_NOTIFY_KEY, key, val);
         }
         if (disp != NULL && disp->slavepid > 1)
            kill (disp->slavepid, SIGUSR2);
      }
   }
}

/**
 * _gdm_set_value_string
 * _gdm_set_value_bool
 * _gdm_set_value_int
 *
 * The following interfaces are used to set values.  The "doing_update"
 * boolean argument which is only set when GDM_UPDATE_CONFIG is called.
 * If doing_update is TRUE, then a notify is sent to slaves.  When
 * loading values at other times (such as when first loading
 * configuration options) there is no need to notify the slaves.  If
 * there is a desire to send a notify to the slaves, the
 * gdm_update_config function should be used instead of calling these
 * functions directly.
 */
static void
_gdm_set_value_string (gchar *key, gchar *value_in, gboolean doing_update)
{
   gchar **setting = gdm_config_hash_lookup (val_hash, key);
   gchar *setting_copy = NULL;
   gchar *temp_string;
   gchar *value;

   if (! ve_string_empty (value_in))
      value = value_in;
   else
      value = NULL;

   if (setting == NULL) {
      gdm_error ("Failure setting key %s to %s", key, value);
      return;
   }

   if (*setting != NULL) {
      /* Free old value */
      setting_copy = g_strdup (*setting);
      g_free (*setting);
   }

   /* User PATH */
   if (is_key (key, GDM_KEY_PATH)) {

      temp_string = gdm_read_default ("PATH=");
      if (temp_string != NULL)
         *setting = temp_string;                
      else if (value != NULL)
         *setting = g_strdup (value);
      else
         *setting = NULL;

   /* Root user PATH */
   } else if (is_key (key, GDM_KEY_ROOT_PATH)) {

      temp_string = gdm_read_default ("SUPATH=");
      if (temp_string != NULL)
         *setting = temp_string;                
      else if (value != NULL)
         *setting = g_strdup (value);
      else
         *setting = NULL;

    /* Location of Xsession script */
    } else if (is_key (key, GDM_KEY_BASE_XSESSION)) {
       if (value != NULL) {
          *setting = g_strdup (value);
       } else {
          gdm_info (_("%s: BaseXsession empty; using %s/gdm/Xsession"),
                      "gdm_config_parse",
                      GDMCONFDIR);
          *setting = g_build_filename (GDMCONFDIR,
                                       "gdm", "Xsession", NULL);
       }

   /* Halt, Reboot, and Suspend commands */
   } else if (is_key (key, GDM_KEY_HALT) ||
              is_key (key, GDM_KEY_REBOOT) ||
              is_key (key, GDM_KEY_SUSPEND)) {
       if (value != NULL)
          *setting = ve_get_first_working_command (value, FALSE);
       else
          *setting = NULL;

   /* Console cannot handle these languages */
   } else if (is_key (key, GDM_KEY_CONSOLE_CANNOT_HANDLE)) {
       if (value != NULL)
          *setting = g_strdup (value);
       else
          *setting = "";
       gdm_ok_console_language ();

   /* Location of Xserver */
   } else if (is_key (key, GDM_KEY_STANDARD_XSERVER)) {
      gchar *bin = NULL;

      if (value != NULL)
         bin = ve_first_word (value);

      if G_UNLIKELY (ve_string_empty (bin) ||
                     g_access (bin, X_OK) != 0) {
         gdm_info (_("%s: Standard X server not found; trying alternatives"),
                     "gdm_config_parse");
         if (g_access ("/usr/X11R6/bin/X", X_OK) == 0) {
            *setting = g_strdup ("/usr/X11R6/bin/X");
         } else if (g_access ("/opt/X11R6/bin/X", X_OK) == 0) {
            *setting = g_strdup ("/opt/X11R6/bin/X");
         } else if (g_access ("/usr/bin/X11/X", X_OK) == 0) {
            *setting = g_strdup ("/usr/bin/X11/X");
         } else
            *setting = g_strdup (value);
      } else {
         *setting = g_strdup (value);
      }

   /* Graphical Theme Directory */
   } else if (is_key (key, GDM_KEY_GRAPHICAL_THEME_DIR)) {
      if (value == NULL ||
          ! g_file_test (value, G_FILE_TEST_IS_DIR))
      {
         *setting = g_strdup (GREETERTHEMEDIR);
      } else {
         *setting = g_strdup (value);
      }

   /* Graphical Theme */
   } else if (is_key (key, GDM_KEY_GRAPHICAL_THEME)) {
     if (value == NULL)
        *setting = g_strdup ("circles");
     else
        *setting = g_strdup (value);
 
   /*
    * Default Welcome Message.  Don't translate here since the
    * GDM user may not be running with the same language as the user.
    * The slave programs will translate the string.
    */
   } else if (is_key (key, GDM_KEY_WELCOME)) {
      if (value != NULL)
         *setting = g_strdup (value);
      else
         *setting = g_strdup (GDM_DEFAULT_WELCOME_MSG);

   /*
    * Default Remote Welcome Message.  Don't translate here since the 
    * GDM user may not be running with the same language as the user.
    * The slave programs will translate the string.
    */
   } else if (is_key (key, GDM_KEY_REMOTE_WELCOME)) {
      if (value != NULL)
         *setting = g_strdup (value);
      else
         *setting = g_strdup (GDM_DEFAULT_REMOTE_WELCOME_MSG);

   /* All others */
   } else {
       if (value != NULL)
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

   /* Handle update */
   if (doing_update == TRUE && 
       strcmp (ve_sure_string (*setting),
               ve_sure_string (setting_copy)) != 0) {

      if (is_key (key, GDM_KEY_GREETER))
         notify_displays_string (GDM_NOTIFY_GREETER, *setting);
      else if (is_key (key, GDM_KEY_REMOTE_GREETER))
         notify_displays_string (GDM_NOTIFY_REMOTE_GREETER, *setting);
      else if (is_key (key, GDM_KEY_SOUND_ON_LOGIN_FILE))
         notify_displays_string (GDM_NOTIFY_SOUND_ON_LOGIN_FILE, *setting);
      else if (is_key (key, GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE))
         notify_displays_string (GDM_NOTIFY_SOUND_ON_LOGIN_SUCCESS_FILE, *setting);
      else if (is_key (key, GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE))
         notify_displays_string (GDM_NOTIFY_SOUND_ON_LOGIN_FAILURE_FILE, *setting);
      else if (is_key (key, GDM_KEY_GTK_MODULES_LIST))
         notify_displays_string (GDM_NOTIFY_GTK_MODULES_LIST, *setting);
      else if (is_key (key, GDM_KEY_TIMED_LOGIN))
         notify_displays_string (GDM_NOTIFY_TIMED_LOGIN, *setting);
   }

   if (setting_copy != NULL)
      g_free (setting_copy);

   if (*setting == NULL)
      gdm_debug ("set config key %s to string <NULL>", key);
   else
      gdm_debug ("set config key %s to string %s", key, *setting);
}

void
gdm_set_value_string (gchar *key, gchar *value_in)
{
   _gdm_set_value_string (key, value_in, TRUE);
}

static void
_gdm_set_value_bool (gchar *key, gboolean value, gboolean doing_update)
{
   gboolean *setting     = gdm_config_hash_lookup (val_hash, key);
   gboolean setting_copy = *setting;
   gchar *temp_string;

   if (setting == NULL) {
      if (value)
         gdm_error ("Failure setting key %s to true", key);
      else
         gdm_error ("Failure setting key %s to false", key);
      return;
   }

   /* Password Required */
   if (is_key (key, GDM_KEY_PASSWORD_REQUIRED)) {
      temp_string = gdm_read_default ("PASSREQ=");
      if (temp_string == NULL)
         *setting = value;
      else if (g_ascii_strcasecmp (temp_string, "YES") == 0)
         *setting = TRUE;
      else
         *setting = FALSE;
      g_free (temp_string);

   /* Allow root login */
   } else if (is_key (key, GDM_KEY_ALLOW_REMOTE_ROOT)) {
      temp_string = gdm_read_default ("CONSOLE=");

      if (temp_string == NULL)
         *setting = value;
      else if (g_ascii_strcasecmp (temp_string, "/dev/console") != 0)
         *setting = TRUE;
      else
         *setting = FALSE;
      g_free (temp_string);

   /* XDMCP */
#ifndef HAVE_LIBXDMCP
   } else if (is_key (key, GDM_KEY_XDMCP)) {
      if (value) {
         gdm_info (_("%s: XDMCP was enabled while there is no XDMCP support; turning it off"), "gdm_config_parse");
      }
      *setting = FALSE;
#endif
   } else {
      *setting = value;
   }

   /* Handle update */
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
     else if (is_key (key, GDM_KEY_TIMED_LOGIN_ENABLE))
        notify_displays_int (GDM_NOTIFY_TIMED_LOGIN_ENABLE, *setting);
   }

   if (*setting)
      gdm_debug ("set config key %s to boolean true", key);
   else
      gdm_debug ("set config key %s to boolean false", key);
}

void
gdm_set_value_bool (gchar *key, gboolean value)
{
   _gdm_set_value_bool (key, value, TRUE);
}

static void
_gdm_set_value_int (gchar *key, gint value, gboolean doing_update)
{
   gint *setting     = gdm_config_hash_lookup (val_hash, key);
   gint setting_copy = *setting;

   if (setting == NULL) {
      gdm_error ("Failure setting key %s to %d", key, value);
      return;
   }

   if (is_key (key, GDM_KEY_MAX_INDIRECT) ||
       is_key (key, GDM_KEY_XINERAMA_SCREEN)) {
      if (value < 0)
         *setting = 0;
      else
         *setting = value;
   } else if (is_key (key, GDM_KEY_TIMED_LOGIN_DELAY)) {
      if (value < 5) {
         gdm_info (_("%s: TimedLoginDelay is less than 5, defaulting to 5."), "gdm_config_parse");
         *setting = 5;
      } else
         *setting = value;
   } else if (is_key (key, GDM_KEY_MAX_ICON_WIDTH) ||
              is_key (key, GDM_KEY_MAX_ICON_HEIGHT)) {
      if (value < 0)
         *setting = 128;
      else
         *setting = value;
   } else if (is_key (key, GDM_KEY_SCAN_TIME)) {
      if (value < 1)
         *setting = 1;
      else
         *setting = value;
   }
 else {
      *setting = value;
   }
 
   /* Handle update */
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
   _gdm_set_value_int (key, value, TRUE);
}

/**
 * gdm_set_value
 *
 * This functon is used to set the config values in the hash.  This is called
 * at initial config load time or when gdm_update_config is called to reload
 * them.   It adds translated strings to the hash with their proper keys
 * (greeter/Welcome[cs] for example).
 */
static gboolean
gdm_set_value (VeConfig *cfg, GdmConfigType *type, gchar *key, gboolean doing_update) 
{
   gchar * realkey = gdm_config_hash_lookup (realkey_hash, key);
   gchar *value;

   if (realkey == NULL) {
      return FALSE;
   }

   if (*type == CONFIG_BOOL) {
       gboolean value = ve_config_get_bool (cfg, realkey);
       _gdm_set_value_bool (key, value, doing_update);
       return TRUE;
   } else if (*type == CONFIG_INT) {
       gint value = ve_config_get_int (cfg, realkey);
       _gdm_set_value_int (key, value, doing_update);
       return TRUE;
   } else if (*type == CONFIG_STRING) {

       /* Process translated strings */
       if (is_key (key, GDM_KEY_WELCOME) ||
           is_key (key, GDM_KEY_REMOTE_WELCOME)) {

          GList *list = ve_config_get_keys (cfg, "greeter");
          gchar *prefix, *basekey;

          if (is_key (key, GDM_KEY_WELCOME)) {
             basekey = g_strdup ("Welcome");
             prefix  = g_strdup ("Welcome[");
          } else {
             basekey = g_strdup ("RemoteWelcome");
             prefix  = g_strdup ("RemoteWelcome[");
          }

          /*
           * Loop over translated keys and put all values into the hash
           * Probably should loop through the hashs and delete any old values,
           * but this just means that if a translation is deleted from the
           * config, GDM won't forget about it until restart.  I don't think
           * this will happen, or be a real problem if it does.
           */
          while (list != NULL) {
             if (g_str_has_prefix ((char *)list->data, prefix) &&
                 g_str_has_suffix ((char *)list->data, "]")) {
                gchar *transkey, *transvalue;

                if (translated_hash == NULL)
                   translated_hash = g_hash_table_new (g_str_hash, g_str_equal);

                transkey   = g_strdup_printf ("greeter/%s", (char *)list->data);
                transvalue = ve_config_get_string (cfg, transkey);

                g_hash_table_remove (translated_hash, transkey);

               /*
                * Store translated values in a separate hash.  Note that we load
                * the initial values via a g_hash_table_foreach function, so if
                * we add these to the same hash, we would end up loading these
                * values in again a second time.
                */
                g_hash_table_insert (translated_hash, transkey, transvalue);
             }
             list = list->next;
          }
          g_free (basekey);
          g_free (prefix);
       }

       /* Handle non-translated strings as normal */
       value = ve_config_get_string (cfg, realkey);
       _gdm_set_value_string (key, value, doing_update);
       return TRUE;
   }
   
   return FALSE;
}

/**
 * gdm_find_xserver
 *
 * Return an xserver with a given ID, or NULL if not found.
 */
GdmXserver *
gdm_find_xserver (const gchar *id)
{
   GSList *li;

   if (xservers == NULL)
      return NULL;

   if (id == NULL)
      return xservers->data;

   for (li = xservers; li != NULL; li = li->next) {
      GdmXserver *svr = li->data;
      if (strcmp (ve_sure_string (svr->id), ve_sure_string (id)) == 0)
         return svr;
   }
   return NULL;
}

/**
 * gdm_get_xservers
 *
 * Prepare a string to be returned for the GET_SERVER_LIST
 * sockets command.
 */
gchar *
gdm_get_xservers (void)
{
   GSList *li;
   gchar *retval = NULL;

   if (xservers == NULL)
      return NULL;

   for (li = xservers; li != NULL; li = li->next) {
      GdmXserver *svr = li->data;
      if (retval != NULL)
         retval = g_strconcat (retval, ";", svr->id, NULL);
      else
         retval = g_strdup (svr->id);
   }

   return retval;
}

/* PRIO_MIN and PRIO_MAX are not defined on Solaris, but are -20 and 20 */
#if sun
#ifndef PRIO_MIN
#define PRIO_MIN -20
#endif
#ifndef PRIO_MAX
#define PRIO_MAX 20
#endif
#endif

/**
 * gdm_load_xservers
 *
 * Load [server-foo] sections from a configuration file.
 */
static void
gdm_load_xservers (VeConfig *cfg)
{
   GList *list, *li;

   /* Find server definitions */
   list = ve_config_get_sections (cfg);
   for (li = list; li != NULL; li = li->next) {
      const gchar *sec = li->data;

      if (strncmp (sec, "server-", strlen ("server-")) == 0) {
         gchar *id;

         id = g_strdup (sec + strlen ("server-"));

         /*
          * See if we already loaded a server with this id, skip if
          * one already exists.
          */
         if (gdm_find_xserver (id) != NULL) {
            g_free (id);
         } else {
            GdmXserver *svr = g_new0 (GdmXserver, 1);
            gchar buf[256];
            int n;

            svr->id = id;

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
            g_snprintf (buf, sizeof (buf), "%s/" GDM_KEY_SERVER_PRIORITY, sec);
            svr->priority = ve_config_get_int (cfg, buf);

            /* do some bounds checking */
             n = svr->priority;
             if (n < PRIO_MIN)
                n = PRIO_MIN;
             else if (n > PRIO_MAX)
                n = PRIO_MAX;
             if (n != svr->priority) {
                gdm_error (_("%s: Priority out of bounds; changed to %d"),
                             "gdm_config_parse", n);
                svr->priority = n;
             }

            if (ve_string_empty (svr->command)) {
               gdm_error (_("%s: Empty server command; "
                            "using standard command."), "gdm_config_parse");
               g_free (svr->command);
               svr->command = g_strdup (GdmStandardXserver);
            }

            xservers = g_slist_append (xservers, svr);
         }
      }
   }
  ve_config_free_list_of_strings (list);
}

/**
 * gdm_update_xservers
 *
 * Reload [server-foo] sections from the configuration files.
 */
static void
gdm_update_xservers (VeConfig *cfg, VeConfig *custom_cfg)
{
   GSList *xli;

   /* Free list if already loaded */
   if (xservers != NULL) {
      for (xli = xservers; xli != NULL; xli = xli->next) {
         GdmXserver *xsvr = xli->data;

         g_free (xsvr->id);
         g_free (xsvr->name);
         g_free (xsvr->command);
      }
      g_slist_free (xservers);
      xservers = NULL;
   }

   /* Reload first from custom_cfg then from cfg. */
   if (custom_cfg != NULL)
      gdm_load_xservers (custom_cfg);

   if (cfg != NULL)
      gdm_load_xservers (cfg);

   /* If no "Standard" server was created, then add it */
   if (xservers == NULL || gdm_find_xserver (GDM_STANDARD) == NULL) {
      GdmXserver *svr = g_new0 (GdmXserver, 1);

      svr->id        = g_strdup (GDM_STANDARD);
      svr->name      = g_strdup ("Standard server");
      svr->command   = g_strdup (GdmStandardXserver);
      svr->flexible  = TRUE;
      svr->choosable = TRUE;
      svr->handled   = TRUE;
      svr->priority  = 0;
      xservers       = g_slist_append (xservers, svr);
   }
}

/**
 * gdm_update_config
 * 
 * Will cause a the GDM daemon to re-read the key from the configuration
 * file and cause notify signal to be sent to the slaves for the 
 * specified key, if appropriate.  Only specific keys defined in the
 * gdm_set_value functions above are associated with such notification.
 * Obviously notification is not needed for configuration options only
 * used by the daemon.  This function is called when the UPDDATE_CONFIG
 * sockets command is called.
 *
 * To add a new notification, a GDM_NOTIFY_* argument will need to be
 * defined in gdm.h, supporting logic placed in the gdm_set_value
 * functions and in the gdm_slave_handle_notify function in slave.c.
 */
gboolean
gdm_update_config (gchar* key)
{
   GdmConfigType *type;
   struct stat statbuf, custom_statbuf;
   VeConfig *cfg;
   VeConfig *custom_cfg = NULL;
   gboolean rc = FALSE;

   /*
    * Do not allow these keys to be updated, since GDM would need
    * additional work, or at least heavy testing, to make these keys
    * flexible enough to be changed at runtime.
    */ 
   if (is_key (key, GDM_KEY_PID_FILE) ||
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

   /* See if custom file is now there */
   if (custom_config_file == NULL) {
      custom_cfg = gdm_get_custom_config (&statbuf);
   }

   /* Don't bother re-reading configuration if files have not changed */
   VE_IGNORE_EINTR (g_stat (config_file, &statbuf));
   VE_IGNORE_EINTR (g_stat (custom_config_file, &custom_statbuf));

  /*
   * Do not reset mtime to the latest values since there is no
   * guarantee that only one key was modified since last write.
   * This check simply avoids re-reading the files if neither
   * has changed since GDM was started.
   */
   if (config_file_mtime        == statbuf.st_mtime &&
       custom_config_file_mtime == custom_statbuf.st_mtime)
      return TRUE;

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
      return TRUE;
   }

   if (custom_config_file != NULL)
      custom_cfg = ve_config_new (custom_config_file);

   if (is_key (key, "xservers/PARAMETERS")) {
      cfg = ve_config_new (config_file);
      gdm_update_xservers (cfg, custom_cfg);
      ve_config_destroy (cfg);
      if (custom_cfg != NULL)
         ve_config_destroy (custom_cfg);
      return TRUE;
   }

   type = gdm_config_hash_lookup (type_hash, key);
   if (type == NULL)
      return FALSE;

   /* First check the custom file */
   if (custom_cfg != NULL) {
       gchar **splitstr = g_strsplit (key, "/", 2);

       if (splitstr[0] != NULL) {
          GList *list = ve_config_get_keys (custom_cfg, splitstr[0]);

          while (list != NULL) {
             gchar *custom_key     = (char *)list->data;
             gchar *custom_fullkey = g_strdup_printf ("%s/%s", splitstr[0], custom_key);

             if (is_key (key, custom_fullkey)) {
                rc = gdm_set_value (custom_cfg, type, key, TRUE);
                
                g_free (custom_fullkey);
                g_strfreev (splitstr);
                ve_config_destroy (custom_cfg);
                return (rc);
             }

             g_free (custom_fullkey);
             list = list->next;
          }
       }
       g_strfreev (splitstr);
   }

   /* If not in the custom file, check main config file */
   cfg = ve_config_new (config_file);
   rc  = gdm_set_value (cfg, type, key, TRUE);

   ve_config_destroy (cfg);
   if (custom_cfg != NULL)
      ve_config_destroy (custom_cfg);
   return rc;
}

/**
 * check_logdir
 * check_servauthdir
 *
 * Support functions for gdm_config_parse.
 */
static void
check_logdir (void)
{
        struct stat statbuf;
        int r;

        VE_IGNORE_EINTR (r = g_stat (GdmLogDir, &statbuf));
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
    VE_IGNORE_EINTR (r = g_stat (GdmServAuthDir, statbuf));
    if G_UNLIKELY (r < 0) {
            if (GdmConsoleNotify) {
               gchar *s = g_strdup_printf
                       (C_(N_("Server Authorization directory "
                              "(daemon/ServAuthDir) is set to %s "
                              "but this does not exist. Please "
                              "correct GDM configuration and "
                              "restart GDM.")), GdmServAuthDir);

               gdm_text_message_dialog (s);
               g_free (s);
            }

            GdmPidFile = NULL;
            gdm_fail (_("%s: Authdir %s does not exist. Aborting."), "gdm_config_parse", GdmServAuthDir);
    }

    if G_UNLIKELY (! S_ISDIR (statbuf->st_mode)) {
            if (GdmConsoleNotify) {
               gchar *s = g_strdup_printf
                       (C_(N_("Server Authorization directory "
                              "(daemon/ServAuthDir) is set to %s "
                              "but this is not a directory. Please "
                              "correct GDM configuration and "
                              "restart GDM.")), GdmServAuthDir);

                gdm_text_message_dialog (s);
                g_free (s);
            }

            GdmPidFile = NULL;
            gdm_fail (_("%s: Authdir %s is not a directory. Aborting."), "gdm_config_parse", GdmServAuthDir);
    }
}

typedef struct _GdmConfigFiles {
   VeConfig *cfg;
   VeConfig *custom_cfg;
} GdmConfigFiles;

/**
 * gdm_load_displays
 *
 * Load the displays section of the config file
 */
static void
gdm_load_displays (VeConfig *cfg, GList *list )
{
   GList *li;
   GSList *li2;

   for (li = list; li != NULL; li = li->next) {
      const gchar *key = li->data;

      if (isdigit (*key)) {
         gchar *fullkey;
         gchar *dispval;
         int keynum = atoi (key);
         gboolean skip_entry = FALSE;

         fullkey  = g_strdup_printf ("%s/%s", GDM_KEY_SECTION_SERVERS, key);
         dispval  = ve_config_get_string (cfg, fullkey);
         g_free (fullkey);

         /* Do not add if already in the list */
         for (li2 = displays; li2 != NULL; li2 = li2->next) {
            GdmDisplay *disp = li2->data;
            if (disp->dispnum == keynum) {
               skip_entry = TRUE;
               break;
            }
         }

         /* Do not add if this display was marked as inactive already */
         for (li2 = displays_inactive; li2 != NULL; li2 = li2->next) {
            gchar *disp = li2->data;
            if (atoi (disp) == keynum) {
               skip_entry = TRUE;
               break;
            }
         }

         if (skip_entry == TRUE) {
            g_free (dispval);
            continue;
         }

         if (g_ascii_strcasecmp (ve_sure_string (dispval), "inactive") == 0) {
            gdm_debug ("display %s is inactive", key);
            displays_inactive = g_slist_append (displays_inactive, g_strdup (key));
         } else {
            GdmDisplay *disp = gdm_server_alloc (keynum, dispval);

            if (disp == NULL)
               continue;

            displays = g_slist_insert_sorted (displays, disp, gdm_compare_displays);
            if (keynum > high_display_num)
               high_display_num = keynum;
         }

         g_free (dispval);

      } else {
        gdm_info (_("%s: Invalid server line in config file. Ignoring!"), "gdm_config_parse");
      }
   }
}

/**
 * gdm_load_config_option
 *
 * Called by gdm_config_parse in a loop to set each key.
 */
static void
gdm_load_config_option (gpointer key_in, gpointer value_in, gpointer data)
{
   gchar *key               = (gchar *)key_in;
   GdmConfigType *type      = (GdmConfigType *)value_in;
   GdmConfigFiles *cfgfiles = (GdmConfigFiles *)data;
   gboolean custom_retval;

   if (type != NULL) {
      /* First check the custom file */
      if (cfgfiles->custom_cfg != NULL) {
          gchar **splitstr = g_strsplit (key_in, "/", 2);
          if (splitstr[0] != NULL) {
             GList *list = ve_config_get_keys (cfgfiles->custom_cfg, splitstr[0]);

             while (list != NULL) {
                gchar *custom_key     = (char *)list->data;
                gchar *custom_fullkey = g_strdup_printf ("%s/%s", splitstr[0], custom_key);

                if (is_key (key_in, custom_fullkey)) {
                   custom_retval = gdm_set_value (cfgfiles->custom_cfg, type, key, FALSE);
                   g_free (custom_fullkey);
                   g_strfreev (splitstr);
                   return;
                }

                g_free (custom_fullkey);
                list = list->next;
             }
          }
          g_strfreev (splitstr);
      }

      /* If not in the custom file, check main config file */
      if (gdm_set_value (cfgfiles->cfg, type, key, FALSE))
         return;
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
   GdmConfigFiles cfgfiles;
   VeConfig *cfg, *custom_cfg;
   struct passwd *pwent;
   struct group *grent;
   struct stat statbuf;
   gchar *bin;

   /* Init structures for configuration data */
   gdm_config_init ();

   displays          = NULL;
   high_display_num  = 0;

   /*
    * It is okay if the custom_cfg file is missing, then just use
    * main configuration file.  If cfg is missing, then GDM will 
    * use the built-in defaults found in gdm.h.
    */
   cfg                      = gdm_get_default_config (&statbuf);
   config_file_mtime        = statbuf.st_mtime;
   custom_cfg               = gdm_get_custom_config (&statbuf);
   custom_config_file_mtime = statbuf.st_mtime;
   cfgfiles.cfg             = cfg;
   cfgfiles.custom_cfg      = custom_cfg;

   /* Loop over all configuration options and load them */
   g_hash_table_foreach (type_hash, gdm_load_config_option, &cfgfiles);

   /* Load server-foo sections */
   gdm_update_xservers (cfg, custom_cfg);

   /* Only read the list if no_console is FALSE at this stage */
   if ( !no_console) {
      GList *list;
      GSList *li2;

      /* Find static X server definitions */
      if (custom_cfg) {
         list = ve_config_get_keys (custom_cfg, GDM_KEY_SECTION_SERVERS);
         gdm_load_displays (custom_cfg, list);
         ve_config_free_list_of_strings (list);
      }

      list = ve_config_get_keys (cfg, GDM_KEY_SECTION_SERVERS);
      gdm_load_displays (cfg, list);
      ve_config_free_list_of_strings (list);

      /* Free list of inactive, not needed anymore */
      for (li2 = displays_inactive; li2 != NULL; li2 = li2->next) {
         gchar *disp = li2->data;
         g_free (disp);
      }
      g_slist_free (displays_inactive);
   }

   if G_UNLIKELY ((displays == NULL) && (! GdmXdmcp) && (!GdmDynamicXservers)) {
      gchar *server = NULL;

      /*
       * If we requested no static servers (there is no console),
       * then don't display errors in console messages
       */
      if (no_console) {
         gdm_fail (_("%s: XDMCP disabled and no static servers defined. Aborting!"), "gdm_config_parse");
      }

      bin = ve_first_word (GdmStandardXserver);
      if G_LIKELY (g_access (bin, X_OK) == 0) {
         server = GdmStandardXserver;
      } else if (g_access ("/usr/bin/X11/X", X_OK) == 0) {
         server = "/usr/bin/X11/X";
      } else if (g_access ("/usr/X11R6/bin/X", X_OK) == 0) {
         server = "/usr/X11R6/bin/X";
      } else if (g_access ("/opt/X11R6/bin/X", X_OK) == 0) {
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
         g_free (GdmTimedLogin);
         GdmAutomaticLogin = NULL;
         GdmTimedLogin     = NULL;
      } else {
         if (GdmConsoleNotify) {
            gchar *s = g_strdup_printf (C_(N_("XDMCP is disabled and GDM "
                                      "cannot find any static server "
                                      "to start.  Aborting!  Please "
                                      "correct the configuration "
                                      "and restart GDM.")));
            gdm_text_message_dialog (s);
            g_free (s);
         }

         GdmPidFile = NULL;
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

      if (GdmConsoleNotify) {
         gchar *s = g_strdup_printf (C_(N_("The GDM user '%s' does not exist. "
                                           "Please correct GDM configuration "
                                           "and restart GDM.")), GdmUser);
         gdm_text_message_dialog (s);
         g_free (s);
      }

      GdmPidFile = NULL;
      gdm_fail (_("%s: Can't find the GDM user '%s'. Aborting!"), "gdm_config_parse", GdmUser);
   } else {
      GdmUserId = pwent->pw_uid;
   }

   if G_UNLIKELY (GdmUserId == 0) {
      if (GdmConsoleNotify) {
         gchar *s = g_strdup_printf (C_(N_("The GDM user is set to be root, but "
                                           "this is not allowed since it can "
                                           "pose a security risk.  Please "
                                           "correct GDM configuration and "
                                           "restart GDM.")));

         gdm_text_message_dialog (s);
         g_free (s);
      }

      GdmPidFile = NULL;
      gdm_fail (_("%s: The GDM user should not be root. Aborting!"), "gdm_config_parse");
   }

   grent = getgrnam (GdmGroup);

   if G_UNLIKELY (grent == NULL) {
      if (GdmConsoleNotify) {
         gchar *s = g_strdup_printf (C_(N_("The GDM group '%s' does not exist. "
                                           "Please correct GDM configuration "
                                           "and restart GDM.")), GdmGroup);
         gdm_text_message_dialog (s);
         g_free (s);
      }

      GdmPidFile = NULL;
      gdm_fail (_("%s: Can't find the GDM group '%s'. Aborting!"), "gdm_config_parse", GdmGroup);
   } else  {
      GdmGroupId = grent->gr_gid;   
   }

   if G_UNLIKELY (GdmGroupId == 0) {
      if (GdmConsoleNotify) {
         gchar *s = g_strdup_printf (C_(N_("The GDM group is set to be root, but "
                                           "this is not allowed since it can "
                                           "pose a security risk. Please "
                                           "correct GDM configuration and "
                                           "restart GDM.")));
         gdm_text_message_dialog (s);
         g_free (s);
      }

      GdmPidFile = NULL;
      gdm_fail (_("%s: The GDM group should not be root. Aborting!"), "gdm_config_parse");
   }

   /* gid remains `gdm' */
   NEVER_FAILS_root_set_euid_egid (GdmUserId, GdmGroupId);

   /* Check that the greeter can be executed */
   bin = ve_first_word (GdmGreeter);
   if G_UNLIKELY (ve_string_empty (bin) || g_access (bin, X_OK) != 0) {
      gdm_error (_("%s: Greeter not found or can't be executed by the GDM user"), "gdm_config_parse");
   }
   g_free (bin);

   bin = ve_first_word (GdmRemoteGreeter);
   if G_UNLIKELY (ve_string_empty (bin) || g_access (bin, X_OK) != 0) {
      gdm_error (_("%s: Remote greeter not found or can't be executed by the GDM user"), "gdm_config_parse");
   }
   g_free (bin);

   /* Check that chooser can be executed */
   bin = ve_first_word (GdmChooser);

   if G_UNLIKELY (GdmIndirect && (ve_string_empty (bin) || g_access (bin, X_OK) != 0)) {
      gdm_error (_("%s: Chooser not found or it can't be executed by the GDM user"), "gdm_config_parse");
   }
    
   g_free (bin);

   /* Check the serv auth and log dirs */
   if G_UNLIKELY (ve_string_empty (GdmServAuthDir)) {
       if (GdmConsoleNotify) {
          gdm_text_message_dialog
             (C_(N_("No daemon/ServAuthDir specified in the GDM configuration file")));
       }
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
   g_chmod (GdmServAuthDir, (S_IRWXU|S_IRWXG|S_ISVTX));

   NEVER_FAILS_root_set_euid_egid (GdmUserId, GdmGroupId);

   /* Again paranoid */
   check_servauthdir (&statbuf);

   if G_UNLIKELY (statbuf.st_uid != 0 || statbuf.st_gid != GdmGroupId)  {
      if (GdmConsoleNotify) {
         gchar *s = g_strdup_printf (C_(N_("Server Authorization directory "
                                           "(daemon/ServAuthDir) is set to %s "
                                           "but is not owned by user %s and group "
                                           "%s. Please correct the ownership or "
                                           "GDM configuration and restart "
                                           "GDM.")), GdmServAuthDir,
		   			gdm_root_user (), GdmGroup);
         gdm_text_message_dialog (s);
         g_free (s);
      }

      GdmPidFile = NULL;
      gdm_fail (_("%s: Authdir %s is not owned by user %s, group %s. Aborting."),
                     "gdm_config_parse", GdmServAuthDir, gdm_root_user (), GdmGroup);
   }

   if G_UNLIKELY (statbuf.st_mode != (S_IFDIR|S_IRWXU|S_IRWXG|S_ISVTX))  {
      if (GdmConsoleNotify) {
         gchar *s = g_strdup_printf (C_(N_("Server Authorization directory "
                                    "(daemon/ServAuthDir) is set to %s "
                                    "but has the wrong permissions: it "
                                    "should have permissions of %o. "
                                    "Please correct the permissions or "
                                    "the GDM configuration and "
                                    "restart GDM.")), GdmServAuthDir,
                                    (S_IRWXU|S_IRWXG|S_ISVTX));
         gdm_text_message_dialog (s);
         g_free (s);
      }

      GdmPidFile = NULL;
      gdm_fail (_("%s: Authdir %s has wrong permissions %o. Should be %o. Aborting."), "gdm_config_parse", 
                  GdmServAuthDir, statbuf.st_mode, (S_IRWXU|S_IRWXG|S_ISVTX));
   }

   NEVER_FAILS_root_set_euid_egid (0, 0);

   check_logdir ();

   /* Check that user authentication is properly configured */
   gdm_verify_check ();

   if (custom_cfg)
      ve_config_destroy (custom_cfg);
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
 * gdm_is_valid_key
 *
 * Returns TRUE if the key is a valid key, FALSE otherwise.
 */
gboolean
gdm_is_valid_key (gchar *key)
{
   GdmConfigType *type = gdm_config_hash_lookup (type_hash, key);
   if (type == NULL)
      return FALSE;

   return TRUE;
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

        if (g_access (path, R_OK) != 0)
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

        if (g_access (path, R_OK) != 0)
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
        if (strncmp (path, PIXMAPDIR, sizeof (PIXMAPDIR)) == 0)
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
                      g_access (picfile, R_OK) != 0)) {
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

                if (g_stat (path, &statbuf) == 0) {
                        char *type = filesystem_type ((char *)path, (char *)path, &statbuf);
                        gboolean is_local = ((strcmp (ve_sure_string (type), "nfs") != 0) &&
                                             (strcmp (ve_sure_string (type), "afs") != 0) &&
                                             (strcmp (ve_sure_string (type), "autofs") != 0) &&
                                             (strcmp (ve_sure_string (type), "unknown") != 0) &&
                                             (strcmp (ve_sure_string (type), "ncpfs") != 0));
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

   /* Only look at local home directories so we don't try to
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
   char *facedir = gdm_get_value_string (GDM_KEY_GLOBAL_FACE_DIR);

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

   if (cached != NULL && strcmp (ve_sure_string (session_name), ve_sure_string (cached)) == 0)
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

   if (ve_string_empty (full) || g_access (full, R_OK) != 0) {
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
   if ((strcmp (ve_sure_string (*usrsess), "Default.desktop") == 0 ||
        strcmp (ve_sure_string (*usrsess), "Default") == 0) &&
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

