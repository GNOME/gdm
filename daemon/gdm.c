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

#include <config.h>
#include <gnome.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <locale.h>

/* This should be moved to auth.c I suppose */
#include <X11/Xauth.h>

#include <vicious.h>

#include "gdm.h"
#include "misc.h"
#include "slave.h"
#include "server.h"
#include "xdmcp.h"
#include "verify.h"
#include "display.h"
#include "choose.h"
#include "errorgui.h"
#include "gdm-net.h"

/* Local functions */
static void gdm_config_parse (void);
static void gdm_handle_message (GdmConnection *conn,
				const char *msg,
				gpointer data);
static void gdm_handle_user_message (GdmConnection *conn,
				     const char *msg,
				     gpointer data);
static void gdm_daemonify (void);
static void gdm_safe_restart (void);
static void gdm_restart_now (void);

/* Global vars */
GSList *displays = NULL;	/* List of displays managed */
GSList *xservers = NULL;	/* List of x server definitions */
gint high_display_num = 0;	/* Highest local non-flexi display */
gint sessions = 0;		/* Number of remote sessions */
gint flexi_servers = 0;		/* Number of flexi servers */
sigset_t sysmask;		/* Inherited system signal mask */
uid_t GdmUserId;		/* Userid under which gdm should run */
gid_t GdmGroupId;		/* Groupid under which gdm should run */
pid_t extra_process = -1;	/* An extra process.  Used for quickie 
				   processes, so that they also get whacked */
int extra_status = 0;		/* Last status from the last extra process */

GdmConnection *fifoconn = NULL; /* Fifo connection */
GdmConnection *unixconn = NULL; /* UNIX Socket connection */

/* True if the server that was run was in actuallity not specified in the
 * config file.  That is if xdmcp was disabled and no local servers were
 * defined.  If the user kills all his local servers by mistake and keeps
 * xdmcp on.  Well then he's screwed.  The configurator should be smarter
 * about that.  But by default xdmcp is disabled so we're likely to catch
 * some fuckups like this. */
gboolean gdm_emergency_server = FALSE;

gboolean gdm_first_login = TRUE;

/* Configuration options */
gchar *GdmUser = NULL;
gchar *GdmGroup = NULL;
gchar *GdmSessDir = NULL;
gchar *GdmLocaleFile = NULL;
gchar *GdmGnomeDefaultSession = NULL;
gchar *GdmAutomaticLogin = NULL;
gboolean GdmAutomaticLoginEnable = FALSE;
gboolean GdmAlwaysRestartServer = FALSE;
gchar *GdmConfigurator = NULL;
gboolean GdmConfigAvailable = FALSE;
gboolean GdmSystemMenu = FALSE;
gboolean GdmBrowser = FALSE;
gchar *GdmGlobalFaceDir = NULL;
gint GdmXineramaScreen = 0;
gchar *GdmGreeter = NULL;
gchar *GdmChooser = NULL;
gchar *GdmLogDir = NULL;
gchar *GdmDisplayInit = NULL;
gchar *GdmSessionDir = NULL;
gchar *GdmPreSession = NULL;
gchar *GdmPostSession = NULL;
gchar *GdmFailsafeXServer = NULL;
gchar *GdmXKeepsCrashing = NULL;
gchar *GdmHalt = NULL;
gchar *GdmReboot = NULL;
gchar *GdmSuspend = NULL;
gchar *GdmServAuthDir = NULL;
gchar *GdmUserAuthDir = NULL;
gchar *GdmUserAuthFile = NULL;
gchar *GdmUserAuthFB = NULL;
gchar *GdmPidFile = NULL;
gchar *GdmDefaultPath = NULL;
gchar *GdmRootPath = NULL;
gchar *GdmDefaultLocale = NULL;
gboolean  GdmKillInitClients = FALSE;
gint  GdmUserMaxFile = 0;
gint  GdmSessionMaxFile = 0;
gboolean  GdmXdmcp = FALSE;
gint  GdmDispPerHost = 0;
gint  GdmMaxPending = 0;
gint  GdmMaxManageWait = 0;
gint  GdmMaxSessions = 0;
gint  GdmPort = 0;
gboolean  GdmIndirect = FALSE;
gint  GdmMaxIndirect = 0;
gint  GdmMaxIndirectWait = 0;
gint  GdmPingInterval = 0;
gchar *GdmWilling = NULL;
gboolean  GdmDebug = FALSE;
gboolean  GdmAllowRoot = FALSE;
gboolean  GdmAllowRemoteRoot = FALSE;
gboolean  GdmAllowRemoteAutoLogin = FALSE;
gint  GdmRelaxPerms = 0;
gint  GdmRetryDelay = 0;
gchar *GdmTimedLogin = NULL;
gboolean GdmTimedLoginEnable = FALSE;
gint GdmTimedLoginDelay = 0;
gchar *GdmStandardXServer = NULL;
gint  GdmFlexibleXServers = 5;
gchar *GdmXnest = NULL;

/* set in the main function */
char **stored_argv = NULL;
int stored_argc = 0;
char *stored_path = NULL;

static gboolean gdm_restart_mode = FALSE;

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

static int
compare_displays (gconstpointer a, gconstpointer b)
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
 * gdm_config_parse:
 *
 * Parse the configuration file and warn about bad permissions etc.
 */

static void 
gdm_config_parse (void)
{
    gchar *k, *v;
    void *iter;
    struct passwd *pwent;
    struct group *grent;
    struct stat statbuf;
    gchar *bin;
    
    displays = NULL;
    high_display_num = 0;

    if (stat (GDM_CONFIG_FILE, &statbuf) == -1)
	gdm_error (_("gdm_config_parse: No configuration file: %s. Using defaults."), GDM_CONFIG_FILE);

    /* Parse configuration options */
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GdmChooser = gnome_config_get_string (GDM_KEY_CHOOSER);
    GdmDefaultPath = gnome_config_get_string (GDM_KEY_PATH);
    GdmDisplayInit = gnome_config_get_string (GDM_KEY_INITDIR);
    GdmAutomaticLoginEnable = gnome_config_get_bool (GDM_KEY_AUTOMATICLOGIN_ENABLE);
    GdmAutomaticLogin = gnome_config_get_string (GDM_KEY_AUTOMATICLOGIN);
    GdmAlwaysRestartServer = gnome_config_get_bool (GDM_KEY_ALWAYSRESTARTSERVER);
    GdmGreeter = gnome_config_get_string (GDM_KEY_GREETER);
    GdmGroup = gnome_config_get_string (GDM_KEY_GROUP);
    GdmHalt = gnome_config_get_string (GDM_KEY_HALT);
    GdmKillInitClients = gnome_config_get_bool (GDM_KEY_KILLIC);
    GdmLogDir= gnome_config_get_string (GDM_KEY_LOGDIR);
    GdmPidFile = gnome_config_get_string (GDM_KEY_PIDFILE);
    GdmSessionDir = gnome_config_get_string (GDM_KEY_SESSDIR);
    GdmPreSession = gnome_config_get_string (GDM_KEY_PRESESS);
    GdmPostSession = gnome_config_get_string (GDM_KEY_POSTSESS);
    GdmFailsafeXServer = gnome_config_get_string (GDM_KEY_FAILSAFE_XSERVER);
    GdmXKeepsCrashing = gnome_config_get_string (GDM_KEY_XKEEPSCRASHING);
    GdmConfigurator = gnome_config_get_string (GDM_KEY_CONFIGURATOR);
    GdmConfigAvailable = gnome_config_get_bool (GDM_KEY_CONFIG_AVAILABLE);
    GdmSystemMenu = gnome_config_get_bool (GDM_KEY_SYSMENU);
    GdmBrowser = gnome_config_get_bool (GDM_KEY_BROWSER);
    GdmGlobalFaceDir = gnome_config_get_string (GDM_KEY_FACEDIR);
    GdmXineramaScreen = gnome_config_get_int (GDM_KEY_XINERAMASCREEN);
    GdmReboot = gnome_config_get_string (GDM_KEY_REBOOT);
    GdmRetryDelay = gnome_config_get_int (GDM_KEY_RETRYDELAY);
    GdmRootPath = gnome_config_get_string (GDM_KEY_ROOTPATH);
    GdmDefaultLocale = gnome_config_get_string (GDM_KEY_LOCALE);
    GdmServAuthDir = gnome_config_get_string (GDM_KEY_SERVAUTH);
    GdmSessDir = gnome_config_get_string (GDM_KEY_SESSDIR);
    GdmSuspend = gnome_config_get_string (GDM_KEY_SUSPEND);
    GdmLocaleFile = gnome_config_get_string (GDM_KEY_LOCFILE);
    GdmGnomeDefaultSession = gnome_config_get_string (GDM_KEY_GNOMEDEFAULTSESSION);
    GdmUser = gnome_config_get_string (GDM_KEY_USER);
    GdmUserAuthDir = gnome_config_get_string (GDM_KEY_UAUTHDIR);
    GdmUserAuthFile = gnome_config_get_string (GDM_KEY_UAUTHFILE);
    GdmUserAuthFB = gnome_config_get_string (GDM_KEY_UAUTHFB);

    GdmTimedLoginEnable = gnome_config_get_bool (GDM_KEY_TIMED_LOGIN_ENABLE);
    GdmTimedLogin = gnome_config_get_string (GDM_KEY_TIMED_LOGIN);
    GdmTimedLoginDelay = gnome_config_get_int (GDM_KEY_TIMED_LOGIN_DELAY);

    GdmAllowRoot = gnome_config_get_bool (GDM_KEY_ALLOWROOT);
    GdmAllowRemoteRoot = gnome_config_get_bool (GDM_KEY_ALLOWREMOTEROOT);
    GdmAllowRemoteAutoLogin = gnome_config_get_bool (GDM_KEY_ALLOWREMOTEAUTOLOGIN);
    GdmRelaxPerms = gnome_config_get_int (GDM_KEY_RELAXPERM);
    GdmUserMaxFile = gnome_config_get_int (GDM_KEY_MAXFILE);
    GdmSessionMaxFile = gnome_config_get_int (GDM_KEY_SESSIONMAXFILE);

    GdmXdmcp = gnome_config_get_bool (GDM_KEY_XDMCP);
    GdmDispPerHost = gnome_config_get_int (GDM_KEY_DISPERHOST);
    GdmMaxPending = gnome_config_get_int (GDM_KEY_MAXPEND);
    GdmMaxManageWait = gnome_config_get_int (GDM_KEY_MAXWAIT);
    GdmMaxSessions = gnome_config_get_int (GDM_KEY_MAXSESS);
    GdmPort = gnome_config_get_int (GDM_KEY_UDPPORT);
    GdmIndirect = gnome_config_get_bool (GDM_KEY_INDIRECT);
    GdmMaxIndirect = gnome_config_get_int (GDM_KEY_MAXINDIR);
    GdmMaxIndirectWait = gnome_config_get_int (GDM_KEY_MAXINDWAIT);    
    GdmPingInterval = gnome_config_get_int (GDM_KEY_PINGINTERVAL);    
    GdmWilling = gnome_config_get_string (GDM_KEY_WILLING);    

    GdmStandardXServer = gnome_config_get_string (GDM_KEY_STANDARD_XSERVER);    
    if (ve_string_empty (GdmStandardXServer)) {
	    g_free (GdmStandardXServer);
	    GdmStandardXServer = g_strdup ("/usr/bin/X11/X");
    }
    GdmFlexibleXServers = gnome_config_get_int (GDM_KEY_FLEXIBLE_XSERVERS);    
    GdmXnest = gnome_config_get_string (GDM_KEY_XNEST);    
    if (ve_string_empty (GdmXnest))
	    GdmXnest = NULL;

    GdmDebug = gnome_config_get_bool (GDM_KEY_DEBUG);

    gnome_config_pop_prefix();

    /* sanitize some values */

#ifndef HAVE_LIBXDMCP
    if (GdmXdmcp) {
	    gdm_info (_("gdm_config_parse: XDMCP was enabled while there is no XDMCP support, turning it off"));
	    GdmXdmcp = FALSE;
    }
#endif

    if ( ! GdmAutomaticLoginEnable ||
	ve_string_empty (GdmAutomaticLogin)) {
	    g_free (GdmAutomaticLogin);
	    GdmAutomaticLogin = NULL;
    }

    if (GdmAutomaticLogin != NULL &&
	strcmp (GdmAutomaticLogin, "root") == 0) {
	    gdm_info (_("gdm_config_parse: Root cannot be autologged in, turing off automatic login"));
	    g_free (GdmAutomaticLogin);
	    GdmAutomaticLogin = NULL;
    }

    if ( ! GdmTimedLoginEnable ||
	ve_string_empty (GdmTimedLogin)) {
	    g_free (GdmTimedLogin);
	    GdmTimedLogin = NULL;
    }

    if (GdmTimedLogin != NULL &&
	strcmp (GdmTimedLogin, "root") == 0) {
	    gdm_info (_("gdm_config_parse: Root cannot be autologged in, turing off timed login"));
	    g_free (GdmTimedLogin);
	    GdmTimedLogin = NULL;
    }

    if (GdmTimedLoginDelay < 5) {
	    gdm_info (_("gdm_config_parse: TimedLoginDelay less then 5, so I will just use 5."));
	    GdmTimedLoginDelay = 5;
    }

    if (GdmMaxIndirect < 0) {
	    GdmMaxIndirect = 0;
    }

    /* Prerequisites */ 
    if (ve_string_empty (GdmGreeter)) {
	    gdm_error (_("gdm_config_parse: No greeter specified."));
    }

    if (ve_string_empty (GdmServAuthDir)) {
	    gdm_text_message_dialog (_("No daemon/ServAuthDir specified in the configuration file"));
	    gdm_fail (_("gdm_config_parse: No authdir specified."));
    }

    if (ve_string_empty (GdmLogDir))
	GdmLogDir = GdmServAuthDir;

    if (ve_string_empty (GdmSessDir))
	gdm_error (_("gdm_config_parse: No sessions directory specified."));

    /* Find server definitions */
    iter = gnome_config_init_iterator_sections ("=" GDM_CONFIG_FILE "=/");
    iter = gnome_config_iterator_next (iter, &k, NULL);
    
    while (iter) {
	    if (strncmp (k, "server-", strlen ("server-")) == 0) {
		    char *section;
		    GdmXServer *svr = g_new0 (GdmXServer, 1);

		    section = g_strdup_printf ("=" GDM_CONFIG_FILE "=/%s/", k);
		    gnome_config_push_prefix (section);

		    svr->id = g_strdup (k + strlen ("server-"));
		    svr->name = gnome_config_get_string (GDM_KEY_SERVER_NAME);
		    svr->command = gnome_config_get_string
			    (GDM_KEY_SERVER_COMMAND);
		    svr->flexible = gnome_config_get_bool
			    (GDM_KEY_SERVER_FLEXIBLE);
		    svr->choosable = gnome_config_get_bool
			    (GDM_KEY_SERVER_CHOOSABLE);

		    if (ve_string_empty (svr->command)) {
			    gdm_error (_("%s: Empty server command, "
					 "using standard one."),
				       "gdm_config_parse");
			    g_free (svr->command);
			    svr->command = g_strdup (GdmStandardXServer);
		    }

		    g_free (section);
		    gnome_config_pop_prefix ();

		    xservers = g_slist_append (xservers, svr);
	    }

	    g_free (k);

	    iter = gnome_config_iterator_next (iter, &k, NULL);
    }

    if (xservers == NULL ||
	gdm_find_x_server (GDM_STANDARD) == NULL) {
	    GdmXServer *svr = g_new0 (GdmXServer, 1);

	    svr->id = g_strdup (GDM_STANDARD);
	    svr->name = g_strdup ("Standard server");
	    svr->command = g_strdup (GdmStandardXServer);
	    svr->flexible = TRUE;
	    svr->choosable = TRUE;

	    xservers = g_slist_append (xservers, svr);
    }

    /* Find local X server definitions */
    iter = gnome_config_init_iterator ("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS);
    iter = gnome_config_iterator_next (iter, &k, &v);
    
    while (iter) {
	    if (isdigit (*k)) {
		    int disp_num = atoi (k);
		    GdmDisplay *disp;

		    while (display_exists (disp_num)) {
			    disp_num++;
		    }

		    if (disp_num != atoi (k)) {
			    gdm_error (_("%s: Display number %d in use!  I will use %d"),
				       "gdm_config_parse", atoi (k), disp_num);
		    }

		    disp = gdm_server_alloc (disp_num, v);
		    if (disp == NULL) {
			    g_free (k);
			    g_free (v);
			    iter = gnome_config_iterator_next (iter, &k, &v);
			    continue;
		    }
		    displays = g_slist_insert_sorted (displays,
						      disp,
						      compare_displays);
		    if (disp_num > high_display_num)
			    high_display_num = disp_num;
	    } else {
		    gdm_info (_("gdm_config_parse: Invalid server line in config file. Ignoring!"));
	    }
	    g_free (k);
	    g_free (v);

	    iter = gnome_config_iterator_next (iter, &k, &v);
    }

    if (displays == NULL && ! GdmXdmcp) {
	    char *server = NULL;
	    if (access (GdmStandardXServer, X_OK) == 0) {
		    server = GdmStandardXServer;
	    } else if (access ("/usr/bin/X11/X", X_OK) == 0) {
		    server = "/usr/bin/X11/X";
	    }
	    /* yay, we can add a backup emergency server */
	    if (server != NULL) {
		    int num = gdm_get_free_display (0 /* start */,
						    0 /* server uid */);
		    gdm_error (_("%s: Xdmcp disabled and no local servers defined. Adding /usr/bin/X11/X on :%d to allow configuration!"),
			       "gdm_config_parse",
			       num);

		    gdm_emergency_server = TRUE;
		    displays = g_slist_append
			    (displays, gdm_server_alloc (num, server));
		    /* ALWAYS run the greeter and don't log anyone in,
		     * this is just an emergency session */
		    g_free (GdmAutomaticLogin);
		    GdmAutomaticLogin = NULL;
		    g_free (GdmTimedLogin);
		    GdmTimedLogin = NULL;
	    } else {
		    char *s = g_strdup_printf (_("Xdmcp is disabled and gdm "
						 "cannot find any local server "
						 "to start.  Aborting!  Please "
						 "correct the configuration %s"
						 "and restart gdm."),
					       GDM_CONFIG_FILE);
		    gdm_text_message_dialog (s);
		    gdm_fail (_("gdm_config_parse: Xdmcp disabled and no local servers defined. Aborting!"));
	    }
    }

    /* Lookup user and groupid for the gdm user */
    pwent = getpwnam (GdmUser);

    if (pwent == NULL) {
	    gdm_error (_("gdm_config_parse: Can't find the gdm user (%s). Trying 'nobody'!"), GdmUser);
	    g_free (GdmUser);
	    GdmUser = g_strdup ("nobody");
	    pwent = getpwnam (GdmUser);
    }

    if (pwent == NULL) {
	    char *s = g_strdup_printf (_("The gdm user does not exist.  "
					 "Please correct gdm configuration %s "
					 "and restart gdm."),
				       GDM_CONFIG_FILE);
	    gdm_text_message_dialog (s);
	    gdm_fail (_("gdm_config_parse: Can't find the gdm user (%s). Aborting!"), GdmUser);
    } else {
	    GdmUserId = pwent->pw_uid;
    }

    if (GdmUserId == 0) {
	    char *s = g_strdup_printf (_("The gdm user is set to be root, but "
					 "this is not allowed since it can "
					 "pose a security risk.  Please "
					 "correct gdm configuration %s and "
					 "restart gdm."), GDM_CONFIG_FILE);
	    gdm_text_message_dialog (s);
	    gdm_fail (_("gdm_config_parse: The gdm user should not be root. Aborting!"));
    }

    grent = getgrnam (GdmGroup);

    if (grent == NULL) {
	    gdm_error (_("gdm_config_parse: Can't find the gdm group (%s). Trying 'nobody'!"), GdmUser);
	    g_free (GdmGroup);
	    GdmGroup = g_strdup ("nobody");
	    pwent = getpwnam (GdmUser);
    }

    if (grent == NULL) {
	    char *s = g_strdup_printf (_("The gdm group does not exist.  "
					 "Please correct gdm configuration %s "
					 "and restart gdm."),
				       GDM_CONFIG_FILE);
	    gdm_text_message_dialog (s);
	    gdm_fail (_("gdm_config_parse: Can't find the gdm group (%s). Aborting!"), GdmGroup);
    } else  {
	    GdmGroupId = grent->gr_gid;   
    }

    if (GdmGroupId == 0) {
	    char *s = g_strdup_printf (_("The gdm group is set to be root, but "
					 "this is not allowed since it can "
					 "pose a security risk.  Please "
					 "correct gdm configuration %s and "
					 "restart gdm."), GDM_CONFIG_FILE);
	    gdm_text_message_dialog (s);
	    gdm_fail (_("gdm_config_parse: The gdm group should not be root. Aborting!"));
    }

    setegid (GdmGroupId);	/* gid remains `gdm' */
    seteuid (GdmUserId);

    /* Check that the greeter can be executed */
    bin = ve_first_word (GdmGreeter);

    if (ve_string_empty (bin) ||
	access (bin, X_OK) != 0) {
	    gdm_error (_("%s: Greeter not found or can't be executed by the gdm user"), "gdm_config_parse");
    }

    g_free (bin);


    /* Check that chooser can be executed */
    bin = ve_first_word (GdmChooser);

    if (GdmIndirect &&
	(ve_string_empty (bin) ||
	 access (bin, X_OK) != 0)) {
	    gdm_error (_("%s: Chooser not found or it can't be executed by the gdm user"), "gdm_config_parse");
    }
    
    g_free (bin);


    /* Enter paranoia mode */
    if (stat (GdmServAuthDir, &statbuf) == -1)  {
	    char *s = g_strdup_printf (_("Server Authorization directory "
					 "(daemon/ServAuthDir) is set to %s "
					 "but this does not exist.  Please "
					 "correct gdm configuration %s and "
					 "restart gdm."), GdmServAuthDir,
				       GDM_CONFIG_FILE);
	    gdm_text_message_dialog (s);
	    gdm_fail (_("gdm_config_parse: Authdir %s does not exist. Aborting."), GdmServAuthDir);
    }

    if (! S_ISDIR (statbuf.st_mode)) {
	    char *s = g_strdup_printf (_("Server Authorization directory "
					 "(daemon/ServAuthDir) is set to %s "
					 "but this is not a directory.  Please "
					 "correct gdm configuration %s and "
					 "restart gdm."), GdmServAuthDir,
				       GDM_CONFIG_FILE);
	    gdm_text_message_dialog (s);
	    gdm_fail (_("gdm_config_parse: Authdir %s is not a directory. Aborting."), GdmServAuthDir);
    }

    if (statbuf.st_uid != GdmUserId || statbuf.st_gid != GdmGroupId)  {
	    char *s = g_strdup_printf (_("Server Authorization directory "
					 "(daemon/ServAuthDir) is set to %s "
					 "but is not owned by user %s and group "
					 "%s.  Please correct the ownership or "
					 "gdm configuration %s and restart "
					 "gdm."),
				       GdmServAuthDir, GdmUser, GdmGroup,
				       GDM_CONFIG_FILE);
	    gdm_text_message_dialog (s);
	    gdm_fail (_("gdm_config_parse: Authdir %s is not owned by user %s, group %s. Aborting."), 
		      GdmServAuthDir, GdmUser, GdmGroup);
    }

    if (statbuf.st_mode != (S_IFDIR|S_IRWXU|S_IRGRP|S_IXGRP))  {
	    char *s = g_strdup_printf (_("Server Authorization directory "
					 "(daemon/ServAuthDir) is set to %s "
					 "but has the wrong permissions, it "
					 "should have permissions of 0750."
					 "Please correct the permissions or "
					 "the gdm configuration %s and "
					 "restart gdm."),
				       GdmServAuthDir, GDM_CONFIG_FILE);
	    gdm_text_message_dialog (s);
	    gdm_fail (_("gdm_config_parse: Authdir %s has wrong permissions %o. Should be 0750. Aborting."), 
		      GdmServAuthDir, statbuf.st_mode);
    }

    seteuid (0);
    setegid (0);

    /* Check that user authentication is properly configured */
    gdm_verify_check ();
}

/* If id == NULL, then get the first X server */
GdmXServer *
gdm_find_x_server (const char *id)
{
	GSList *li;

	if (xservers == NULL)
		return NULL;

	if (id == NULL)
		return xservers->data;

	for (li = xservers; li != NULL; li = li->next) {
		GdmXServer *svr = li->data;
		if (strcmp (svr->id, id) == 0)
			return svr;
	}
	return NULL;
}

/**
 * gdm_daemonify:
 *
 * Detach gdm daemon from the controlling terminal
 */

static void
gdm_daemonify (void)
{
    FILE *pf;
    pid_t pid;

    pid = fork ();
    if (pid > 0) {

	if ((pf = fopen (GdmPidFile, "w"))) {
	    fprintf (pf, "%d\n", pid);
	    fclose (pf);
	}

        exit (EXIT_SUCCESS);
    }

    if (pid < 0) 
	gdm_fail (_("gdm_daemonify: fork() failed!"));

    if (setsid() < 0)
	gdm_fail (_("gdm_daemonify: setsid() failed: %s!"), strerror(errno));

    chdir (GdmServAuthDir);
    umask (022);

    close (0);
    close (1);
    close (2);

    open("/dev/null", O_RDONLY); /* open stdin - fd 0 */
    open("/dev/null", O_RDWR); /* open stdout - fd 1 */
    open("/dev/null", O_RDWR); /* open stderr - fd 2 */
}

static void 
gdm_start_first_unborn_local (int delay)
{
	GSList *li;

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *d = li->data;

		if (d != NULL &&
		    d->type == TYPE_LOCAL &&
		    d->dispstat == DISPLAY_UNBORN) {
			gdm_debug ("gdm_start_first_unborn_local: "
				   "Starting %s", d->name);

			/* well sleep at least 'delay' seconds
			 * before starting */
			d->sleep_before_run = delay;

			/* only the first local display has
			 * timed login going on */
			if (gdm_first_login)
				d->timed_login_ok = TRUE;

			if ( ! gdm_display_manage (d)) {
				gdm_display_unmanage (d);
				/* only the first local display gets
				 * autologged in */
				gdm_first_login = FALSE;
			} else {
				break;
			}
		}
	}
}

static void
final_cleanup (void)
{
	GSList *list;
	sigset_t mask;

	gdm_debug ("final_cleanup");

	sigemptyset (&mask);
	sigaddset (&mask, SIGCHLD);
	sigprocmask (SIG_BLOCK, &mask, NULL); 

	if (extra_process > 1) {
		/* we sigterm extra processes, and we
		 * don't wait */
		kill (extra_process, SIGTERM);
		extra_process = -1;
	}

	list = g_slist_copy (displays);
	g_slist_foreach (list, (GFunc) gdm_display_unmanage, NULL);
	g_slist_free (list);

	/* Close stuff */

	gdm_xdmcp_close ();

	if (fifoconn != NULL) {
		char *path;
		gdm_connection_close (fifoconn);
		path = g_strconcat (GdmServAuthDir, "/.gdmfifo", NULL);
		unlink (path);
		g_free (path);
		fifoconn = NULL;
	}

	if (unixconn != NULL) {
		gdm_connection_close (unixconn);
		unlink (GDM_SUP_SOCKET);
		unixconn = NULL;
	}

	closelog();

	if (GdmPidFile != NULL)
		unlink (GdmPidFile);
}

static gboolean
deal_with_x_crashes (GdmDisplay *d)
{
    gboolean just_abort = FALSE;

    if ( ! d->failsafe_xserver &&
	 ! ve_string_empty (GdmFailsafeXServer)) {
	    char *bin = ve_first_word (GdmFailsafeXServer);
	    /* Yay we have a failsafe */
	    if ( ! ve_string_empty (bin) &&
		access (bin, X_OK) == 0) {
		    gdm_info (_("deal_with_x_crashes: Trying failsafe X "
				"server %s"), GdmFailsafeXServer);
		    g_free (bin);
		    g_free (d->command);
		    d->command = g_strdup (GdmFailsafeXServer);
		    d->failsafe_xserver = TRUE;
		    return TRUE;
	    }
	    g_free (bin);
    }

    /* Eeek X keeps crashing, let's try the XKeepsCrashing script */
    if ( ! ve_string_empty (GdmXKeepsCrashing) &&
	access (GdmXKeepsCrashing, X_OK|R_OK) == 0) {
	    pid_t pid;

	    gdm_info (_("deal_with_x_crashes: Running the "
			"XKeepsCrashing script"));

	    pid = gdm_fork_extra ();

	    if (pid == 0) {
		    char *argv[2];
		    char *xlog = g_strconcat (GdmLogDir, "/", d->name, ".log", NULL);
		    int ii;

		    for (ii = 0; ii < sysconf (_SC_OPEN_MAX); ii++)
			    close (ii);

		    /* No error checking here - if it's messed the best response
		    * is to ignore & try to continue */
		    open ("/dev/null", O_RDONLY); /* open stdin - fd 0 */
		    open ("/dev/null", O_RDWR); /* open stdout - fd 1 */
		    open ("/dev/null", O_RDWR); /* open stderr - fd 2 */

		    argv[0] = GdmXKeepsCrashing;
		    argv[1] = NULL;

		    /* unset DISPLAY and XAUTHORITY if they exist
		     * so that gdialog (if used) doesn't get confused */
		    ve_unsetenv ("DISPLAY");
		    ve_unsetenv ("XAUTHORITY");

		    /* some promised variables */
		    ve_setenv ("XLOG", xlog, TRUE);
		    ve_setenv ("BINDIR", EXPANDED_BINDIR, TRUE);
		    ve_setenv ("SBINDIR", EXPANDED_SBINDIR, TRUE);

		    /* To enable gettext stuff in the script */
		    ve_setenv ("TEXTDOMAIN", PACKAGE, TRUE);
		    ve_setenv ("TEXTDOMAINDIR", GNOMELOCALEDIR, TRUE);

		    execv (argv[0], argv);
	
		    /* yaikes! */
		    _exit (32);
	    } else if (pid > 0) {
		    int status;

		    gdm_wait_for_extra (&status);

		    if (WIFEXITED (status) &&
			WEXITSTATUS (status) == 0) {
			    /* Yay, the user wants to try again, so
			     * here we go */
			    return TRUE;
		    } else if (WIFEXITED (status) &&
			       WEXITSTATUS (status) == 32) {
			    /* We couldn't run the script, just drop through */
			    ;
		    } else {
			    /* shit went wrong, or the user's a wanker */
			    just_abort = TRUE;
		    }
	    }

	    /* if we failed to fork, or something else has happened,
	     * we fall through to the other options below */
    }


    /* if we have "open" we can talk to the user, not as user
     * friendly as the above script, but getting there */
    if ( ! just_abort &&
	access (EXPANDED_SBINDIR "/gdmopen", X_OK) == 0) {
	    /* Shit if we knew what the program was to tell the user,
	     * the above script would have been defined and we'd run
	     * it for them */
	    char *error = _("I cannot start the X server (your graphical "
			    "interface).  It is likely that it is not set "
			    "up correctly.  You will need to log in on a "
			    "console and rerun the X configuration "
			    "program.  Then restart GDM.");
	    gdm_text_message_dialog (error);
    } /* else {
       * At this point .... screw the user, we don't know how to
       * talk to him.  He's on some 'l33t system anyway, so syslog
       * reading will do him good 
       * } */

    gdm_error (_("Failed to start X server several times in a short time period; disabling display %s"), d->name);

    return FALSE;
}

static gboolean
bin_executable (const char *command)
{
	char **argv;

	if (ve_string_empty (command))
		return FALSE;

	argv = ve_split (command);
	if (argv != NULL &&
	    argv[0] != NULL &&
	    access (argv[0], X_OK) == 0) {
		g_strfreev (argv);
		return TRUE;
	} else {
		g_strfreev (argv);
		return FALSE;
	}
}

static gboolean 
gdm_cleanup_children (void)
{
    pid_t pid;
    gint exitstatus = 0, status;
    GdmDisplay *d = NULL;
    gchar **argv;
    gboolean crashed;

    /* Pid and exit status of slave that died */
    pid = waitpid (-1, &exitstatus, WNOHANG);

    if (pid <= 0)
	    return FALSE;

    if (WIFEXITED (exitstatus)) {
	    status = WEXITSTATUS (exitstatus);
	    crashed = FALSE;
    } else {
	    status = EXIT_SUCCESS;
	    crashed = TRUE;
    }
	
    gdm_debug ("gdm_cleanup_children: child %d returned %d%s", pid, status,
	       crashed ? " (child crashed)" : "");

    if (pid == extra_process) {
	    /* an extra process died, yay! */
	    extra_process = -1;
	    extra_status = exitstatus;
	    return TRUE;
    }

    /* Find out who this slave belongs to */
    d = gdm_display_lookup (pid);

    if (d == NULL)
	    return TRUE;

    if (crashed) {
	    gdm_debug ("gdm_cleanup_children: Slave crashed, killing it's "
		       "children");

	    if (d->sesspid > 0) {
		    if (kill (d->sesspid, SIGTERM) == 0)
			    waitpid (d->sesspid, NULL, 0);
		    d->sesspid = 0;
	    }
	    if (d->greetpid > 0) {
		    if (kill (d->greetpid, SIGTERM) == 0)
			    waitpid (d->greetpid, NULL, 0);
		    d->greetpid = 0;
	    }
	    if (d->chooserpid > 0) {
		    if (kill (d->chooserpid, SIGTERM) == 0)
			    waitpid (d->chooserpid, NULL, 0);
		    d->chooserpid = 0;
	    }
	    if (d->servpid > 0) {
		    if (kill (d->servpid, SIGTERM) == 0)
			    waitpid (d->servpid, NULL, 0);
		    d->servpid = 0;
	    }
    }

    /* null all these, they are not valid most definately */
    d->servpid = 0;
    d->sesspid = 0;
    d->greetpid = 0;
    d->chooserpid = 0;

    /* definately not logged in now */
    d->logged_in = FALSE;

    /* Declare the display dead */
    d->slavepid = 0;
    d->dispstat = DISPLAY_DEAD;

    if ( ! GdmSystemMenu &&
	(status == DISPLAY_REBOOT ||
	 status == DISPLAY_HALT)) {
	    gdm_info (_("gdm_child_action: Reboot or Halt request when there is no system menu from display %s"), d->name);
	    status = DISPLAY_REMANAGE;
    }

    if ( ! d->console &&
	(status == DISPLAY_RESTARTGDM ||
	 status == DISPLAY_REBOOT ||
	 status == DISPLAY_HALT)) {
	    gdm_info (_("gdm_child_action: Restart, Reboot or Halt request from a non-local display %s"), d->name);
	    status = DISPLAY_REMANAGE;
    }

    if (status == DISPLAY_CHOSEN) {
	    /* forget about this indirect id, since this
	     * display will be dead very soon, and we don't want it
	     * to take the indirect display with it */
	    d->indirect_id = 0;
	    status = DISPLAY_REMANAGE;
    }

    /* checkout if we can actually do stuff */
    switch (status) {
    case DISPLAY_REBOOT:
	    if ( ! bin_executable (GdmReboot))
		    status = DISPLAY_REMANAGE;
	    break;
    case DISPLAY_HALT:
	    if ( ! bin_executable (GdmHalt))
		    status = DISPLAY_REMANAGE;
	    break;
    case DISPLAY_SUSPEND:
	    if ( ! bin_executable (GdmSuspend))
		    status = DISPLAY_REMANAGE;
	    break;
    default:
	    break;
    }

    /* Autopsy */
    switch (status) {
	
    case DISPLAY_ABORT:		/* Bury this display for good */
	gdm_info (_("gdm_child_action: Aborting display %s"), d->name);

	if (gdm_restart_mode)
		gdm_safe_restart ();

	gdm_display_unmanage (d);

	/* If there are some pending locals, start them now */
	gdm_start_first_unborn_local (3 /* delay */);
	break;
	
    case DISPLAY_REBOOT:	/* Reboot machine */
	gdm_info (_("gdm_child_action: Master rebooting..."));

	final_cleanup ();

	argv = ve_split (GdmReboot);
	execv (argv[0], argv);

	gdm_error (_("gdm_child_action: Reboot failed: %s"), strerror (errno));
	break;
	
    case DISPLAY_HALT:		/* Halt machine */
	gdm_info (_("gdm_child_action: Master halting..."));

	final_cleanup ();

	argv = ve_split (GdmHalt);
	execv (argv[0], argv);

	gdm_error (_("gdm_child_action: Halt failed: %s"), strerror (errno));
	break;

    case DISPLAY_SUSPEND:	/* Suspend machine */
	gdm_info (_("gdm_child_action: Master suspending..."));

	final_cleanup ();

	argv = ve_split (GdmSuspend);
	execv (argv[0], argv);

	gdm_error (_("gdm_child_action: Suspend failed: %s"), strerror (errno));
	break;

    case DISPLAY_RESTARTGDM:
	gdm_restart_now ();
	break;

    case DISPLAY_XFAILED:       /* X sucks */
	/* inform about error if needed */
	if (d->socket_conn != NULL) {
		GdmConnection *conn = d->socket_conn;
		d->socket_conn = NULL;
		gdm_connection_set_close_notify (conn, NULL, NULL);
		gdm_connection_write (conn, "ERROR 3 X failed\n");
	}

	if (gdm_restart_mode)
		gdm_safe_restart ();

	/* in remote/flexi case just drop to _REMANAGE */
	if (d->type == TYPE_LOCAL) {
		time_t now = time (NULL);
		d->x_faileds ++;
		/* this is a much faster failing, don't even allow the 8
		 * seconds, just flash but for at most 30 seconds */
		if (now - d->last_x_failed > 30) {
			/* reset */
			d->x_faileds = 1;
			d->last_x_failed = now;
		} else if (d->x_faileds > 3) {
			gdm_debug ("gdm_child_action: dealing with X crashes");
			if ( ! deal_with_x_crashes (d)) {
				gdm_debug ("gdm_child_action: Aborting display");
				/* an original way to deal with these things:
				 * "Screw you guys, I'm going home!" */
				gdm_display_unmanage (d);

				/* If there are some pending locals,
				 * start them now */
				gdm_start_first_unborn_local (3 /* delay */);
				break;
			}
			gdm_debug ("gdm_child_action: Trying again");
		} else {
			/* well sleep at least 3 seconds before starting */
			d->sleep_before_run = 3;
		}
		/* go around the display loop detection, we're doing
		 * our own here */
		d->last_start_time = 0;
	}
	/* fall through */

    case DISPLAY_REMANAGE:	/* Remanage display */
    default:
	gdm_debug ("gdm_child_action: Slave process returned %d", status);

	/* inform about error if needed */
	if (d->socket_conn != NULL) {
		GdmConnection *conn = d->socket_conn;
		d->socket_conn = NULL;
		gdm_connection_set_close_notify (conn, NULL, NULL);
		gdm_connection_write (conn, "ERROR 2 Startup errors\n");
	}

	if (gdm_restart_mode)
		gdm_safe_restart ();
	
	/* This is a local server so we start a new slave */
	if (d->type == TYPE_LOCAL) {
		if ( ! gdm_display_manage (d)) {
			gdm_display_unmanage (d);
			/* If there are some pending locals,
			 * start them now */
			gdm_start_first_unborn_local (3 /* delay */);
		}
	/* Remote displays will send a request to be managed */
	} else /* TYPE_XDMCP, TYPE_FLEXI, TYPE_FLEXI_XNEST */ {
		gdm_display_unmanage (d);
	}
	
	break;
    }

    if (gdm_restart_mode)
	    gdm_safe_restart ();

    gdm_quit ();

    return TRUE;
}

static void
gdm_restart_now (void)
{
	gdm_info (_("Gdm restarting ..."));
	final_cleanup ();
	if (stored_path != NULL)
		ve_setenv ("PATH", stored_path, TRUE);
	execvp (stored_argv[0], stored_argv);
	gdm_error (_("Failed to restart self"));
	_exit (1);
}

static void
gdm_safe_restart (void)
{
	GSList *li;

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *d = li->data;

		if (d->logged_in)
			return;
	}

	gdm_restart_now ();
}

static gboolean
mainloop_sig_callback (gint8 sig, gpointer data)
{
  gdm_debug ("mainloop_sig_callback: Got signal %d", (int)sig);
  switch (sig)
    {
    case SIGCHLD:
      while (gdm_cleanup_children ())
	      ;
      break;

    case SIGINT:
    case SIGTERM:
      gdm_debug ("mainloop_sig_callback: Got TERM/INT. Going down!");
      final_cleanup ();
      exit (EXIT_SUCCESS);
      break;

    case SIGHUP:
      gdm_restart_now ();
      break;

    case SIGUSR1:
      gdm_restart_mode = TRUE;
      gdm_safe_restart ();
      break;
 
    default:
      break;
    }
  
  return TRUE;
}

static void
signal_notify (int sig)
{
	g_signal_notify (sig);
}

/*
 * main: The main daemon control
 */

static GMainLoop *main_loop;    

static void
store_argv (int argc, char *argv[])
{
	int i;

	stored_path = g_strdup (g_getenv ("PATH"));

	stored_argv = g_new0 (char *, argc + 1);
	for (i = 0; i < argc; i++)
		stored_argv[i] = g_strdup (argv[i]);
	stored_argv[i] = NULL;
	stored_argc = argc;
}

static void
create_connections (void)
{
	gchar *path;

	path = g_strconcat (GdmServAuthDir, "/.gdmfifo", NULL);
	fifoconn = gdm_connection_open_fifo (path, 0660);
	g_free (path);

	if (fifoconn != NULL)
		gdm_connection_set_handler (fifoconn,
					    gdm_handle_message,
					    NULL /* data */,
					    NULL /* destroy_notify */);

	unixconn = gdm_connection_open_unix (GDM_SUP_SOCKET, 0666);

	if (unixconn != NULL)
		gdm_connection_set_handler (unixconn,
					    gdm_handle_user_message,
					    NULL /* data */,
					    NULL /* destroy_notify */);
}

int 
main (int argc, char *argv[])
{
    sigset_t mask;
    struct sigaction term, child;
    FILE *pf;

    store_argv (argc, argv);

    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, GNOMELOCALEDIR);
    textdomain(PACKAGE);

    /* This is an utter hack.  BUT we don't want to have the message in
     * another program as it is supposed to be a last ditch effort to talk
     * to the user, so what happens is that we respawn ourselves with
     * an argument --run-error-dialog error dialog_type <x>:<y>:<width>:<height>
     * the coordinates are the coordinates of this screen (when using
     * xinerama) they can all be 0 */
    if (argc == 5 &&
	strcmp (argv[1], "--run-error-dialog") == 0) {
	    int x = 0, y = 0, width = 0, height = 0;
	    sscanf (argv[4], "%d:%d:%d:%d", &x, &y, &width, &height);
	    gdm_run_errorgui (argv[2], argv[3], x, y, width, height);
	    _exit (0);
    }
    /* This is another utter hack.  Same as above but for questions */
    if (argc == 5 &&
	strcmp (argv[1], "--run-failsafe-question") == 0) {
	    int x = 0, y = 0, width = 0, height = 0;
	    gboolean echo;
	    char *ret;
	    sscanf (argv[4], "%d:%d:%d:%d", &x, &y, &width, &height);
	    if (strcmp (argv[3], "FALSE") == 0)
		    echo = FALSE;
	    else
		    echo = TRUE;
	    ret = gdm_run_failsafe_question (argv[2], echo, x, y, width, height);
	    if (ret != NULL) {
		    g_print (ret);
		    fflush (stdout);
	    }
	    _exit (0);
    }
    /* This is another utter hack.  Same as above but for yes/no questions */
    if (argc == 4 &&
	strcmp (argv[1], "--run-failsafe-yesno") == 0) {
	    int x = 0, y = 0, width = 0, height = 0;
	    gboolean ret;
	    sscanf (argv[3], "%d:%d:%d:%d", &x, &y, &width, &height);
	    ret = gdm_run_failsafe_yesno (argv[2], x, y, width, height);
	    if (ret)
		    g_print ("yes\n");
	    else
		    g_print ("no\n");
	    fflush (stdout);
	    _exit (0);
    }


    /* XDM compliant error message */
    if (getuid() != 0) {
	    /* make sure the pid file doesn't get wiped */
	    GdmPidFile = NULL;
	    gdm_fail (_("Only root wants to run gdm\n"));
    }


    /* Initialize runtime environment */
    umask (022);
    gnome_do_not_create_directories = TRUE;
    gnomelib_init ("gdm", VERSION);
    main_loop = g_main_new (FALSE);
    openlog ("gdm", LOG_PID, LOG_DAEMON);


    /* Parse configuration file */
    gdm_config_parse();

    /* Check if another gdm process is already running */
    if (access (GdmPidFile, R_OK) == 0) {

        /* Check if the existing process is still alive. */
        gint pidv;

        pf = fopen (GdmPidFile, "r");

        if (pf != NULL &&
	    fscanf (pf, "%d", &pidv) == 1 &&
	    kill (pidv, 0) != -1) {
		/* make sure the pid file doesn't get wiped */
		GdmPidFile = NULL;
		gdm_fail (_("gdm already running. Aborting!"));
	}

	if (pf != NULL)
		fclose (pf);
    }

    /* Become daemon unless started in -nodaemon mode or child of init */
    if ( (argc == 2 && strcmp (argv[1], "-nodaemon") == 0) || getppid() == 1) {

	/* Write pid to pidfile */
	if ((pf = fopen (GdmPidFile, "w"))) {
	    fprintf (pf, "%d\n", getpid());
	    fclose (pf);
	}
    }
    else
	gdm_daemonify();

    /* Signal handling */
    g_signal_add (SIGCHLD, mainloop_sig_callback, NULL);
    g_signal_add (SIGTERM, mainloop_sig_callback, NULL);
    g_signal_add (SIGINT, mainloop_sig_callback, NULL);
    g_signal_add (SIGHUP, mainloop_sig_callback, NULL);
    g_signal_add (SIGUSR1, mainloop_sig_callback, NULL);
    
    term.sa_handler = signal_notify;
    term.sa_flags = SA_RESTART;
    sigemptyset (&term.sa_mask);

    if (sigaction (SIGTERM, &term, NULL) < 0) 
	gdm_fail (_("%s: Error setting up TERM signal handler"),
		  "gdm_main");

    if (sigaction (SIGINT, &term, NULL) < 0) 
	gdm_fail (_("%s: Error setting up INT signal handler"),
		  "gdm_main");

    if (sigaction (SIGHUP, &term, NULL) < 0) 
	gdm_fail (_("%s: Error setting up HUP signal handler"),
		  "gdm_main");

    if (sigaction (SIGUSR1, &term, NULL) < 0) 
	gdm_fail (_("%s: Error setting up USR1 signal handler"),
		  "gdm_main");

    child.sa_handler = signal_notify;
    child.sa_flags = SA_RESTART|SA_NOCLDSTOP;
    sigemptyset (&child.sa_mask);
    sigaddset (&child.sa_mask, SIGCHLD);

    if (sigaction (SIGCHLD, &child, NULL) < 0) 
	gdm_fail (_("%s: Error setting up CHLD signal handler"), "gdm_main");

    sigemptyset (&mask);
    sigaddset (&mask, SIGINT);
    sigaddset (&mask, SIGTERM);
    sigaddset (&mask, SIGCHLD);
    sigaddset (&mask, SIGHUP);
    sigaddset (&mask, SIGUSR1);
    sigprocmask (SIG_UNBLOCK, &mask, &sysmask); /* Save system sigmask */

    gdm_debug ("gdm_main: Here we go...");

    /* Init XDMCP if applicable */
    if (GdmXdmcp)
	gdm_xdmcp_init();

    create_connections ();

    /* Start local X servers */
    gdm_start_first_unborn_local (0 /* delay */);

    /* Accept remote connections */
    if (GdmXdmcp) {
	gdm_debug ("Accepting XDMCP connections...");
	gdm_xdmcp_run();
    }

    /* We always exit via exit(), and sadly we need to g_main_quit()
     * at times not knowing if it's this main or a recursive one we're
     * quitting.
     */
    while (1)
      {
        gdm_run ();
	gdm_debug ("main: Exited main loop");
      }

    return EXIT_SUCCESS;	/* Not reached */
}

/* signal main loop support */


typedef struct _GSignalData GSignalData;
struct _GSignalData
{
  guint8      index;
  guint8      shift;
  GSignalFunc callback;
};

static gboolean g_signal_prepare  (gpointer  source_data,
				   GTimeVal *current_time,
				   gint     *timeout,
				   gpointer   user_data);
static gboolean g_signal_check    (gpointer  source_data,
				   GTimeVal *current_time,
				   gpointer  user_data);
static gboolean g_signal_dispatch (gpointer  source_data,
				   GTimeVal *current_time,
				   gpointer  user_data);

static GSourceFuncs signal_funcs = {
  g_signal_prepare,
  g_signal_check,
  g_signal_dispatch,
  g_free
};
static	guint32	signals_notified[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

static gboolean
g_signal_prepare (gpointer  source_data,
		  GTimeVal *current_time,
		  gint     *timeout,
		  gpointer  user_data)
{
  GSignalData *signal_data = source_data;
  
  return signals_notified[signal_data->index] & (1 << signal_data->shift);
}

static gboolean
g_signal_check (gpointer  source_data,
		GTimeVal *current_time,
		gpointer  user_data)
{
  GSignalData *signal_data = source_data;
  
  return signals_notified[signal_data->index] & (1 << signal_data->shift);
}

static gboolean
g_signal_dispatch (gpointer  source_data,
		   GTimeVal *current_time,
		   gpointer  user_data)
{
  GSignalData *signal_data = source_data;
  
  signals_notified[signal_data->index] &= ~(1 << signal_data->shift);
  
  return signal_data->callback (-128 + signal_data->index * 32 + signal_data->shift, user_data);
}

guint
g_signal_add (gint8	  signal,
	      GSignalFunc function,
	      gpointer    data)
{
  return g_signal_add_full (G_PRIORITY_DEFAULT, signal, function, data, NULL);
}

guint
g_signal_add_full (gint           priority,
		   gint8          signal,
		   GSignalFunc    function,
		   gpointer       data,
		   GDestroyNotify destroy)
{
  GSignalData *signal_data;
  guint s = 128 + signal;
  
  g_return_val_if_fail (function != NULL, 0);
  
  signal_data = g_new (GSignalData, 1);
  signal_data->index = s / 32;
  signal_data->shift = s % 32;
  signal_data->callback = function;
  
  return g_source_add (priority, TRUE, &signal_funcs, signal_data, data, destroy);
}

void
g_signal_notify (gint8 signal)
{
  guint index, shift;
  guint s = 128 + signal;
  
  index = s / 32;
  shift = s % 32;
  
  signals_notified[index] |= 1 << shift;
}

void
gdm_run (void)
{
  g_main_run (main_loop);
}

void
gdm_quit (void)
{
  g_main_quit (main_loop);
}

static void
gdm_handle_message (GdmConnection *conn, const char *msg, gpointer data)
{
	/* Evil!, all this for debugging? */
	if (GdmDebug) {
		if (strncmp (msg, GDM_SOP_COOKIE " ",
			     strlen (GDM_SOP_COOKIE " ")) == 0) {
			char *s = g_strndup
				(msg, strlen (GDM_SOP_COOKIE " XXXX XX"));
			/* cut off most of the cookie for "security" */
			gdm_debug ("Handeling message: '%s...'", s);
			g_free (s);
		} else {
			gdm_debug ("Handeling message: '%s'", msg);
		}
	}

	if (strncmp (msg, GDM_SOP_CHOSEN " ",
		     strlen (GDM_SOP_CHOSEN " ")) == 0) {
		gdm_choose_data (msg);
	} else if (strncmp (msg, GDM_SOP_XPID " ",
		            strlen (GDM_SOP_XPID " ")) == 0) {
		GdmDisplay *d;
		long slave_pid, pid;

		if (sscanf (msg, GDM_SOP_XPID " %ld %ld", &slave_pid, &pid)
		    != 2)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			d->servpid = pid;
			gdm_debug ("Got XPID == %ld", (long)pid);
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_SESSPID " ",
		            strlen (GDM_SOP_SESSPID " ")) == 0) {
		GdmDisplay *d;
		long slave_pid, pid;

		if (sscanf (msg, GDM_SOP_SESSPID " %ld %ld", &slave_pid, &pid)
		    != 2)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			d->sesspid = pid;
			gdm_debug ("Got SESSPID == %ld", (long)pid);
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_GREETPID " ",
		            strlen (GDM_SOP_GREETPID " ")) == 0) {
		GdmDisplay *d;
		long slave_pid, pid;

		if (sscanf (msg, GDM_SOP_GREETPID " %ld %ld", &slave_pid, &pid)
		    != 2)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			d->greetpid = pid;
			gdm_debug ("Got GREETPID == %ld", (long)pid);
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_CHOOSERPID " ",
		            strlen (GDM_SOP_CHOOSERPID " ")) == 0) {
		GdmDisplay *d;
		long slave_pid, pid;

		if (sscanf (msg, GDM_SOP_CHOOSERPID " %ld %ld",
			    &slave_pid, &pid) != 2)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			d->chooserpid = pid;
			gdm_debug ("Got CHOOSERPID == %ld", (long)pid);
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_LOGGED_IN " ",
		            strlen (GDM_SOP_LOGGED_IN " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;
		int logged_in;
		if (sscanf (msg, GDM_SOP_LOGGED_IN " %ld %d", &slave_pid,
			    &logged_in) != 2)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			d->logged_in = logged_in ? TRUE : FALSE;
			gdm_debug ("Got logged in == %s",
				   d->logged_in ? "TRUE" : "FALSE");

			/* if the user just logged out,
			 * let's see if it's safe to restart */
			if (gdm_restart_mode &&
			    ! d->logged_in)
				gdm_safe_restart ();

			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_DISP_NUM " ",
		            strlen (GDM_SOP_DISP_NUM " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;
		int disp_num;

		if (sscanf (msg, GDM_SOP_DISP_NUM " %ld %d",
			    &slave_pid, &disp_num) != 2)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			g_free (d->name);
			d->name = g_strdup_printf (":%d", disp_num);
			d->dispnum = disp_num;
			gdm_debug ("Got DISP_NUM == %d", disp_num);
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_VT_NUM " ",
		            strlen (GDM_SOP_VT_NUM " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;
		int vt_num;

		if (sscanf (msg, GDM_SOP_VT_NUM " %ld %d",
			    &slave_pid, &vt_num) != 2)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
#ifdef __linux__
			d->vt = vt_num;
#endif
			gdm_debug ("Got VT_NUM == %d", vt_num);
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_LOGIN " ",
		            strlen (GDM_SOP_LOGIN " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;
		char *p;

		if (sscanf (msg, GDM_SOP_LOGIN " %ld",
			    &slave_pid) != 1)
			return;
		p = strchr (msg, ' ');
		if (p != NULL)
			p = strchr (p+1, ' ');
		if (p == NULL)
			return;

		p++;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			g_free (d->login);
			d->login = g_strdup (p);
			gdm_debug ("Got LOGIN == %s", p);
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_COOKIE " ",
		            strlen (GDM_SOP_COOKIE " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;
		char *p;

		if (sscanf (msg, GDM_SOP_COOKIE " %ld",
			    &slave_pid) != 1)
			return;
		p = strchr (msg, ' ');
		if (p != NULL)
			p = strchr (p+1, ' ');
		if (p == NULL)
			return;

		p++;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			g_free (d->cookie);
			d->cookie = g_strdup (p);
			gdm_debug ("Got COOKIE == <secret>");
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_FLEXI_ERR " ",
		            strlen (GDM_SOP_FLEXI_ERR " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;
		int err;

		if (sscanf (msg, GDM_SOP_FLEXI_ERR " %ld %d",
			    &slave_pid, &err) != 2)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			char *error = NULL;
			GdmConnection *conn = d->socket_conn;
			d->socket_conn = NULL;

			if (conn != NULL)
				gdm_connection_set_close_notify (conn,
								 NULL, NULL);

			if (err == 3)
				error = "ERROR 3 X failed\n";
			else if (err == 4)
				error = "ERROR 4 X too busy\n";
			else if (err == 5)
				error = "ERROR 5 Xnest can't connect\n";
			else
				error = "ERROR 999 Unknown error\n";
			if (conn != NULL)
				gdm_connection_write (conn, error);
			
			gdm_debug ("Got FLEXI_ERR == %d", err);
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strncmp (msg, GDM_SOP_FLEXI_OK " ",
		            strlen (GDM_SOP_FLEXI_OK " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;

		if (sscanf (msg, GDM_SOP_FLEXI_OK " %ld",
			    &slave_pid) != 1)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			GdmConnection *conn = d->socket_conn;
			d->socket_conn = NULL;

			if (conn != NULL) {
				char *msg = g_strdup_printf
					("OK %s\n", d->name);
				gdm_connection_set_close_notify (conn,
								 NULL, NULL);
				if ( ! gdm_connection_write (conn, msg))
					gdm_display_unmanage (d);
				g_free (msg);
			}
			
			gdm_debug ("Got FLEXI_OK");
			/* send ack */
			kill (slave_pid, SIGUSR2);
		}
	} else if (strcmp (msg, GDM_SOP_SOFT_RESTART) == 0) {
		gdm_restart_mode = TRUE;
		gdm_safe_restart ();
	} else if (strcmp (msg, GDM_SOP_START_NEXT_LOCAL) == 0) {
		gdm_start_first_unborn_local (3 /* delay */);
	}
}

/* extract second word and the rest of the string */
static void
extract_dispname_xauthfile_cookie (const char *msg,
				   char **dispname,
				   char **xauthfile,
				   char **cookie)
{
	const char *p;
	char *pp;

	*dispname = NULL;
	*xauthfile = NULL;
	*cookie = NULL;

	/* Get dispname */
	p = strchr (msg, ' ');
	if (p == NULL)
		return;

	while (*p == ' ')
		p++;

	*dispname = g_strdup (p);
	pp = strchr (*dispname, ' ');
	if (pp != NULL)
		*pp = '\0';

	/* Get cookie */
	p = strchr (p, ' ');
	if (p == NULL) {
		*dispname = NULL;
		g_free (*dispname);
		return;
	}
	while (*p == ' ')
		p++;

	*cookie = g_strdup (p);
	pp = strchr (*cookie, ' ');
	if (pp != NULL)
		*pp = '\0';

	/* Get xauthfile */
	p = strchr (p, ' ');
	if (p == NULL) {
		*cookie = NULL;
		g_free (*cookie);
		*dispname = NULL;
		g_free (*dispname);
		return;
	}

	while (*p == ' ')
		p++;

	*xauthfile = g_strstrip (g_strdup (p));

}

static void
close_conn (gpointer data)
{
	GdmDisplay *disp = data;

	/* We still weren't finished, so we want to whack this display */
	if (disp->socket_conn != NULL) {
		disp->socket_conn = NULL;
		gdm_display_unmanage (disp);
	}
}

static GdmDisplay *
find_display (const char *name)
{
	GSList *li;

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *disp = li->data;
		if (disp->name != NULL &&
		    strcmp (disp->name, name) == 0)
			return disp;
	}
	return NULL;
}

static char *
extract_dispnum (const char *addy)
{
	int num;
	char *p;

	g_assert (addy != NULL);

	p = strchr (addy, ':');
	if (p == NULL)
		return NULL;

	/* Whee! handles DECnet even if we don't do that */
	while (*p == ':')
		p++;

	if (sscanf (p, "%d", &num) != 1)
		return NULL;

	return g_strdup_printf ("%d", num);
}

static char *
dehex_cookie (const char *cookie, int *len)
{
	/* it should be +1 really, but I'm paranoid */
	char *bcookie = g_new0 (char, (strlen (cookie) / 2) + 2);
	int i;
	const char *p;

	*len = 0;

	for (i = 0, p = cookie;
	     *p != '\0' && *(p+1) != '\0';
	     i++, p += 2) {
		int num;
		if (sscanf (p, "%02x", &num) != 1) {
			g_free (bcookie);
			return NULL;
		}
		bcookie[i] = num;
	}
	*len = i;
	return bcookie;
}

static gboolean
check_cookie (const char *file, const char *disp, const char *cookie)
{
	Xauth *xa;
	char *number;
	char *bcookie;
	int cookielen;
	gboolean ret = FALSE;

	FILE *fp = fopen (file, "r");
	if (fp == NULL)
		return FALSE;

	number = extract_dispnum (disp);
	if (number == NULL)
		return FALSE;
	bcookie = dehex_cookie (cookie, &cookielen);
	if (bcookie == NULL) {
		g_free (number);
		return FALSE;
	}

	while ((xa = XauReadAuth (fp)) != NULL) {
		if (xa->number_length == strlen (number) &&
		    strncmp (xa->number, number, xa->number_length) == 0 &&
		    xa->name_length == strlen ("MIT-MAGIC-COOKIE-1") &&
		    strncmp (xa->name, "MIT-MAGIC-COOKIE-1",
			     xa->name_length) == 0 &&
		    xa->data_length == cookielen &&
		    memcmp (xa->data, bcookie, cookielen) == 0) {
			XauDisposeAuth (xa);
			ret = TRUE;
			break; 
		}
		XauDisposeAuth (xa);
	}

	g_free (number);
	g_free (bcookie);

	fclose (fp);

	return ret;
}

static void
handle_flexi_server (GdmConnection *conn, int type, const char *server,
		     const char *xnest_disp, const char *xnest_auth_file,
		     const char *xnest_cookie)
{
	GdmDisplay *display;
	char *bin;
	uid_t server_uid = 0;

	gdm_debug ("server: '%s'", server);

	if (type == TYPE_FLEXI_XNEST) {
		struct stat s;
		gboolean authorized = TRUE;

		g_assert (xnest_auth_file != NULL);
		g_assert (xnest_disp != NULL);
		g_assert (xnest_cookie != NULL);

		if (stat (xnest_auth_file, &s) < 0)
			authorized = FALSE;
		if (authorized &&
		    /* if readable or writable by group or others,
		     * we are NOT authorized */
		    s.st_mode & 0077)
			authorized = FALSE;
		if (authorized &&
		    ! check_cookie (xnest_auth_file,
				    xnest_disp,
				    xnest_cookie)) {
			authorized = FALSE;
		}

		if ( ! authorized) {
			/* Sorry dude, you're not doing something
			 * right */
			gdm_connection_write (conn,
					      "ERROR 100 Not authenticated\n");
			return;
		}

		server_uid = s.st_uid;
	}

	if (flexi_servers >= GdmFlexibleXServers) {
		gdm_connection_write (conn,
				      "ERROR 1 No more flexi servers\n");
		return;
	}

	bin = ve_first_word (server);
	if (ve_string_empty (server) ||
	    access (bin, X_OK) != 0) {
		g_free (bin);
		gdm_connection_write (conn,
				      "ERROR 6 No server binary\n");
		return;
	}
	g_free (bin);

	display = gdm_server_alloc (-1, server);
	if (display == NULL) {
		gdm_connection_write (conn,
				      "ERROR 2 Startup errors\n");
		return;
	}

	if (type == TYPE_FLEXI_XNEST) {
		GdmDisplay *parent;
		char *disp, *p;
		g_assert (xnest_disp != NULL);

		disp = g_strdup (xnest_disp);
		/* whack the screen info */
		p = strchr (disp, ':');
		if (p != NULL)
			p = strchr (p+1, '.');
		if (p != NULL)
			*p = '\0';
		/* if it's on one of the console displays we started,
		 * it's on the console, else it's not (it could be but
		 * we aren't sure and we don't want to be fooled) */
		parent = find_display (disp);
		if (/* paranoia */xnest_disp[0] == ':' &&
		    parent != NULL &&
		    parent->console)
			display->console = TRUE;
		else
			display->console = FALSE;
		g_free (disp);

		display->server_uid = server_uid;
	}

	flexi_servers ++;

	display->type = type;
	display->socket_conn = conn;
	display->xnest_disp = g_strdup (xnest_disp);
	display->xnest_auth_file = g_strdup (xnest_auth_file);
	gdm_connection_set_close_notify (conn, display, close_conn);
	displays = g_slist_append (displays, display);
	if ( ! gdm_display_manage (display)) {
		gdm_display_unmanage (display);
		gdm_connection_write (conn,
				      "ERROR 2 Startup errors\n");
		return;
	}
	/* Now we wait for the server to start up (or not) */
}

static void
gdm_handle_user_message (GdmConnection *conn, const char *msg, gpointer data)
{
	gdm_debug ("Handeling user message: '%s'", msg);

	if (strncmp (msg, GDM_SUP_AUTH_LOCAL " ",
		     strlen (GDM_SUP_AUTH_LOCAL " ")) == 0) {
		GSList *li;
		char *cookie = g_strdup
			(&msg[strlen (GDM_SUP_AUTH_LOCAL " ")]);
		g_strstrip (cookie);
		if (strlen (cookie) != 16*2) /* 16 bytes in hex form */ {
			/* evil, just whack the connection in this case */
			gdm_connection_write (conn,
					      "ERROR 100 Not authenticated\n");
			gdm_connection_close (conn);
			g_free (cookie);
			return;
		}
		/* check if cookie matches one of the console displays */
		for (li = displays; li != NULL; li = li->next) {
			GdmDisplay *disp = li->data;
			if (disp->console &&
			    disp->cookie != NULL &&
			    strcmp (disp->cookie, cookie) == 0) {
				g_free (cookie);
				GDM_CONNECTION_SET_USER_FLAG
					(conn, GDM_SUP_FLAG_AUTHENTICATED);
				gdm_connection_write (conn, "OK\n");
				return;
			}
		}

		/* Hmmm, perhaps this is better defined behaviour */
		GDM_CONNECTION_UNSET_USER_FLAG
			(conn, GDM_SUP_FLAG_AUTHENTICATED);
		gdm_connection_write (conn, "ERROR 100 Not authenticated\n");
		g_free (cookie);
	} else if (strcmp (msg, GDM_SUP_FLEXI_XSERVER) == 0) {
		/* Only allow locally authenticated connections */
		if ( ! (gdm_connection_get_user_flags (conn) &
			GDM_SUP_FLAG_AUTHENTICATED)) {
			gdm_info (_("Flexible server request denied: "
				    "Not authenticated"));
			gdm_connection_write (conn,
					      "ERROR 100 Not authenticated\n");
			return;
		}
		handle_flexi_server (conn, TYPE_FLEXI, GdmStandardXServer,
				     NULL, NULL, NULL);
	} else if (strncmp (msg, GDM_SUP_FLEXI_XSERVER " ",
		            strlen (GDM_SUP_FLEXI_XSERVER " ")) == 0) {
		char *name;
		const char *command = NULL;
		GdmXServer *svr;

		/* Only allow locally authenticated connections */
		if ( ! (gdm_connection_get_user_flags (conn) &
			GDM_SUP_FLAG_AUTHENTICATED)) {
			gdm_info (_("Flexible server request denied: "
				    "Not authenticated"));
			gdm_connection_write (conn,
					      "ERROR 100 Not authenticated\n");
			return;
		}

		name = g_strdup (&msg[strlen (GDM_SUP_FLEXI_XSERVER " ")]);
		g_strstrip (name);
		if (ve_string_empty (name)) {
			g_free (name);
			name = g_strdup (GDM_STANDARD);
		}

		svr = gdm_find_x_server (name);
		if (svr == NULL) {
			/* Don't print the name to syslog as it might be
			 * long and dangerous */
			gdm_error (_("Unknown server type requested, using "
				     "standard server."));
			command = GdmStandardXServer;
		} else if ( ! svr->flexible) {
			gdm_error (_("Requested server %s not allowed to be "
				     "used for flexible servers, using "
				     "standard server."), name);
			command = GdmStandardXServer;
		} else {
			command = svr->command;
		}
		g_free (name);

		handle_flexi_server (conn, TYPE_FLEXI, command,
				     NULL, NULL, NULL);
	} else if (strncmp (msg, GDM_SUP_FLEXI_XNEST " ",
		            strlen (GDM_SUP_FLEXI_XNEST " ")) == 0) {
		char *dispname = NULL, *xauthfile = NULL, *cookie = NULL;

		extract_dispname_xauthfile_cookie (msg, &dispname, &xauthfile, &cookie);

		if (dispname == NULL) {
			/* Something bogus is going on, so just whack the
			 * connection */
			g_free (xauthfile);
			gdm_connection_close (conn);
			return;
		}

		/* This is probably a pre-2.2.4.2 client */
		if (xauthfile == NULL || cookie == NULL) {
			/* evil, just whack the connection in this case */
			gdm_connection_write (conn,
					      "ERROR 100 Not authenticated\n");
			gdm_connection_close (conn);
			g_free (cookie);
			return;
		}
		
		handle_flexi_server (conn, TYPE_FLEXI_XNEST, GdmXnest,
				     dispname, xauthfile, cookie);

		g_free (dispname);
		g_free (xauthfile);
	} else if (strcmp (msg, GDM_SUP_CONSOLE_SERVERS) == 0) {
		GSList *li;
		const char *sep = " ";
		gdm_connection_write (conn, "OK");
		for (li = displays; li != NULL; li = li->next) {
			GdmDisplay *disp = li->data;
			if ( ! disp->console)
				continue;
			gdm_connection_write (conn, sep);
			sep = ";";
			gdm_connection_write (conn,
					      ve_sure_string (disp->name));
			gdm_connection_write (conn, ",");
			gdm_connection_write (conn,
					      ve_sure_string (disp->login));
			gdm_connection_write (conn, ",");
			if (disp->type == TYPE_FLEXI_XNEST) {
				gdm_connection_write
					(conn,
					 ve_sure_string (disp->xnest_disp));
			} else {
				char *vt = g_strdup_printf ("%d",
#ifdef __linux__
							    disp->vt
#else
							    -1
#endif
							    );
				gdm_connection_write (conn, vt);
				g_free (vt);
			}
		}
		gdm_connection_write (conn, "\n");
	} else if (strcmp (msg, GDM_SUP_VERSION) == 0) {
		gdm_connection_write (conn, "GDM " VERSION "\n");
	} else if (strcmp (msg, GDM_SUP_CLOSE) == 0) {
		gdm_connection_close (conn);
	} else {
		gdm_connection_write (conn, "ERROR 0 Not implemented\n");
		gdm_connection_close (conn);
	}
}

/* EOF */
