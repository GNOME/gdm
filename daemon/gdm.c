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
#include <libgnome/libgnome.h>
#include <signal.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(_POSIX_PRIORITY_SCHEDULING) && defined(HAVE_SCHED_YIELD)
#include <sched.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/resource.h>
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
#include "getvt.h"
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
gint xdmcp_sessions = 0;	/* Number of remote sessions */
gint xdmcp_pending = 0;		/* Number of pending remote sessions */
gint flexi_servers = 0;		/* Number of flexi servers */
uid_t GdmUserId;		/* Userid under which gdm should run */
gid_t GdmGroupId;		/* Groupid under which gdm should run */
pid_t extra_process = 0;	/* An extra process.  Used for quickie 
				   processes, so that they also get whacked */
int extra_status = 0;		/* Last status from the last extra process */
pid_t gdm_main_pid = 0;		/* PID of the main daemon */
gboolean preserve_ld_vars = FALSE; /* Preserve the ld environment variables */
gboolean no_daemon = FALSE;	/* Do not daemonize */
gboolean no_console = FALSE;	/* There are no local servers, this means,
				   don't run local servers and second,
				   don't display info on the console */

GdmConnection *fifoconn = NULL; /* Fifo connection */
GdmConnection *pipeconn = NULL; /* slavepipe (handled just like Fifo for compatibility) connection */
GdmConnection *unixconn = NULL; /* UNIX Socket connection */
int slave_fifo_pipe_fd = -1; /* the slavepipe connection */

char *gdm_charset = NULL;

int gdm_normal_runlevel = -1; /* runlevel on linux that gdm was started in */

/* True if the server that was run was in actuallity not specified in the
 * config file.  That is if xdmcp was disabled and no local servers were
 * defined.  If the user kills all his local servers by mistake and keeps
 * xdmcp on.  Well then he's screwed.  The configurator should be smarter
 * about that.  But by default xdmcp is disabled so we're likely to catch
 * some fuckups like this. */
gboolean gdm_emergency_server = FALSE;

gboolean gdm_first_login = TRUE;

int gdm_in_signal = 0;
gboolean gdm_in_final_cleanup = FALSE;

/* Configuration options */
gchar *GdmUser = NULL;
gchar *GdmGroup = NULL;
gchar *GdmGtkRC = NULL;
gchar *GdmSessDir = NULL;
gchar *GdmXsession = NULL;
gchar *GdmLocaleFile = NULL;
gchar *GdmAutomaticLogin = NULL;
gboolean GdmAutomaticLoginEnable = FALSE;
gboolean GdmAlwaysRestartServer = FALSE;
gchar *GdmConfigurator = NULL;
gboolean GdmConfigAvailable = FALSE;
gboolean GdmSystemMenu = FALSE;
gboolean GdmChooserButton = FALSE;
gboolean GdmBrowser = FALSE;
gboolean GdmAddGtkModules = FALSE;
gboolean GdmDoubleLoginWarning = TRUE;
gchar *GdmGlobalFaceDir = NULL;
gint GdmXineramaScreen = 0;
gchar *GdmGreeter = NULL;
gchar *GdmRemoteGreeter = NULL;
gchar *GdmGtkModulesList = NULL;
gchar *GdmChooser = NULL;
gchar *GdmLogDir = NULL;
gchar *GdmDisplayInit = NULL;
gchar *GdmSessionDir = NULL;
gchar *GdmPostLogin = NULL;
gchar *GdmPreSession = NULL;
gchar *GdmPostSession = NULL;
gchar *GdmFailsafeXServer = NULL;
gchar *GdmXKeepsCrashing = NULL;
gchar *GdmHalt = NULL;
gchar *GdmHaltReal = NULL;
gchar *GdmReboot = NULL;
gchar *GdmRebootReal = NULL;
gchar *GdmSuspend = NULL;
gchar *GdmSuspendReal = NULL;
gchar *GdmServAuthDir = NULL;
gchar *GdmUserAuthDir = NULL;
gchar *GdmUserAuthFile = NULL;
gchar *GdmUserAuthFB = NULL;
gchar *GdmPidFile = NULL;
gchar *GdmDefaultPath = NULL;
gchar *GdmRootPath = NULL;
gboolean  GdmKillInitClients = FALSE;
gint  GdmUserMaxFile = 0;
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
int GdmFirstVT = 7;
gboolean GdmVTAllocation = TRUE;
gboolean GdmDisallowTCP = TRUE;


/* set in the main function */
char **stored_argv = NULL;
int stored_argc = 0;

static time_t config_file_mtime = 0;

static gboolean gdm_restart_mode = FALSE;

static GMainLoop *main_loop = NULL;

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

static void
check_servauthdir (struct stat *statbuf)
{
    int r;

    /* Enter paranoia mode */
    IGNORE_EINTR (r = stat (GdmServAuthDir, statbuf));
    if (r < 0) {
	    char *s = g_strdup_printf
		    (_("Server Authorization directory "
		       "(daemon/ServAuthDir) is set to %s "
		       "but this does not exist. Please "
		       "correct gdm configuration %s and "
		       "restart gdm."), GdmServAuthDir,
		     GDM_CONFIG_FILE);
	    if ( ! no_console)
		    gdm_text_message_dialog (s);
	    GdmPidFile = NULL;
	    gdm_fail (_("%s: Authdir %s does not exist. Aborting."), "gdm_config_parse", GdmServAuthDir);
    }

    if (! S_ISDIR (statbuf->st_mode)) {
	    char *s = g_strdup_printf
		    (_("Server Authorization directory "
		       "(daemon/ServAuthDir) is set to %s "
		       "but this is not a directory. Please "
		       "correct gdm configuration %s and "
		       "restart gdm."), GdmServAuthDir,
		     GDM_CONFIG_FILE);
	    if ( ! no_console)
		    gdm_text_message_dialog (s);
	    GdmPidFile = NULL;
	    gdm_fail (_("%s: Authdir %s is not a directory. Aborting."), "gdm_config_parse", GdmServAuthDir);
    }
}

static void
check_logdir (void)
{
	struct stat statbuf;
	int r;
   
	IGNORE_EINTR (r = stat (GdmLogDir, &statbuf));
	if (r < 0 ||
	    ! S_ISDIR (statbuf.st_mode))  {
		gdm_error (_("%s: Logdir %s does not exist or isn't a directory.  Using ServAuthDir %s."), "gdm_config_parse",
			   GdmLogDir, GdmServAuthDir);
		g_free (GdmLogDir);
		GdmLogDir = g_strdup (GdmServAuthDir);
	}
}

/**
 * gdm_config_parse:
 *
 * Parse the configuration file and warn about bad permissions etc.
 */

static void 
gdm_config_parse (void)
{
    struct passwd *pwent;
    struct group *grent;
    struct stat statbuf;
    gchar *bin;
    VeConfig *cfg;
    GList *list, *li;
    int r;
    
    displays = NULL;
    high_display_num = 0;

#if defined(_SCO_DS) || defined(__UNIXWARE__) || defined(__USLC__)
    gdm_error ("SCO OS detected!!!  I will sleep for 5 seconds in protest.  This message can be disabled by SCO stopping to sue people.");
    sleep (5);
#endif

    IGNORE_EINTR (r = stat (GDM_CONFIG_FILE, &statbuf));
    if (r < 0) {
	    gdm_error (_("%s: No configuration file: %s. Using defaults."),
		       "gdm_config_parse", GDM_CONFIG_FILE);
    } else {
	    config_file_mtime = statbuf.st_mtime;
    }

    /* Parse configuration options */
    cfg = ve_config_new (GDM_CONFIG_FILE);

    GdmChooser = ve_config_get_string (cfg, GDM_KEY_CHOOSER);
    GdmDefaultPath = ve_config_get_string (cfg, GDM_KEY_PATH);
    GdmDisplayInit = ve_config_get_string (cfg, GDM_KEY_INITDIR);
    GdmAutomaticLoginEnable = ve_config_get_bool (cfg, GDM_KEY_AUTOMATICLOGIN_ENABLE);
    GdmAutomaticLogin = ve_config_get_string (cfg, GDM_KEY_AUTOMATICLOGIN);
    GdmAlwaysRestartServer = ve_config_get_bool (cfg, GDM_KEY_ALWAYSRESTARTSERVER);
    GdmGreeter = ve_config_get_string (cfg, GDM_KEY_GREETER);
    GdmRemoteGreeter = ve_config_get_string (cfg, GDM_KEY_REMOTEGREETER);
    GdmAddGtkModules = ve_config_get_bool (cfg, GDM_KEY_ADD_GTK_MODULES);
    GdmDoubleLoginWarning = ve_config_get_bool (cfg, GDM_KEY_DOUBLELOGINWARNING);
    GdmGtkModulesList = ve_config_get_string (cfg, GDM_KEY_GTK_MODULES_LIST);	
    GdmGroup = ve_config_get_string (cfg, GDM_KEY_GROUP);
    GdmHalt = ve_config_get_string (cfg, GDM_KEY_HALT);
    GdmKillInitClients = ve_config_get_bool (cfg, GDM_KEY_KILLIC);
    GdmLogDir= ve_config_get_string (cfg, GDM_KEY_LOGDIR);
    GdmPidFile = ve_config_get_string (cfg, GDM_KEY_PIDFILE);
    GdmSessionDir = ve_config_get_string (cfg, GDM_KEY_SESSDIR);
    GdmPostLogin = ve_config_get_string (cfg, GDM_KEY_POSTLOGIN);
    GdmPreSession = ve_config_get_string (cfg, GDM_KEY_PRESESS);
    GdmPostSession = ve_config_get_string (cfg, GDM_KEY_POSTSESS);
    GdmFailsafeXServer = ve_config_get_string (cfg, GDM_KEY_FAILSAFE_XSERVER);
    GdmXKeepsCrashing = ve_config_get_string (cfg, GDM_KEY_XKEEPSCRASHING);
    GdmConfigurator = ve_config_get_string (cfg, GDM_KEY_CONFIGURATOR);
    GdmConfigAvailable = ve_config_get_bool (cfg, GDM_KEY_CONFIG_AVAILABLE);
    GdmSystemMenu = ve_config_get_bool (cfg, GDM_KEY_SYSMENU);
    GdmChooserButton = ve_config_get_bool (cfg, GDM_KEY_CHOOSER_BUTTON);
    GdmBrowser = ve_config_get_bool (cfg, GDM_KEY_BROWSER);
    GdmGlobalFaceDir = ve_config_get_string (cfg, GDM_KEY_FACEDIR);
    GdmXineramaScreen = ve_config_get_int (cfg, GDM_KEY_XINERAMASCREEN);
    GdmReboot = ve_config_get_string (cfg, GDM_KEY_REBOOT);
    GdmRetryDelay = ve_config_get_int (cfg, GDM_KEY_RETRYDELAY);
    GdmRootPath = ve_config_get_string (cfg, GDM_KEY_ROOTPATH);
    GdmServAuthDir = ve_config_get_string (cfg, GDM_KEY_SERVAUTH);
    GdmSessDir = ve_config_get_string (cfg, GDM_KEY_SESSDIR);
    GdmXsession = ve_config_get_string (cfg, GDM_KEY_BASEXSESSION);
    if (ve_string_empty (GdmXsession)) {
	    gdm_info (_("%s: BaseXsession empty, using %s/gdm/Xsession"),
			"gdm_config_parse",
			EXPANDED_SYSCONFDIR);
	    g_free (GdmXsession);
	    GdmXsession = g_strconcat (EXPANDED_SYSCONFDIR, "/gdm/Xsession",
				       NULL);
    }
    GdmSuspend = ve_config_get_string (cfg, GDM_KEY_SUSPEND);
    GdmLocaleFile = ve_config_get_string (cfg, GDM_KEY_LOCFILE);
    GdmUser = ve_config_get_string (cfg, GDM_KEY_USER);
    GdmUserAuthDir = ve_config_get_string (cfg, GDM_KEY_UAUTHDIR);
    GdmUserAuthFile = ve_config_get_string (cfg, GDM_KEY_UAUTHFILE);
    GdmUserAuthFB = ve_config_get_string (cfg, GDM_KEY_UAUTHFB);

    GdmGtkRC = ve_config_get_string (cfg, GDM_KEY_GTKRC);

    GdmTimedLoginEnable = ve_config_get_bool (cfg, GDM_KEY_TIMED_LOGIN_ENABLE);
    GdmTimedLogin = ve_config_get_string (cfg, GDM_KEY_TIMED_LOGIN);
    GdmTimedLoginDelay = ve_config_get_int (cfg, GDM_KEY_TIMED_LOGIN_DELAY);

    GdmAllowRoot = ve_config_get_bool (cfg, GDM_KEY_ALLOWROOT);
    GdmAllowRemoteRoot = ve_config_get_bool (cfg, GDM_KEY_ALLOWREMOTEROOT);
    GdmAllowRemoteAutoLogin = ve_config_get_bool (cfg, GDM_KEY_ALLOWREMOTEAUTOLOGIN);
    GdmRelaxPerms = ve_config_get_int (cfg, GDM_KEY_RELAXPERM);
    GdmUserMaxFile = ve_config_get_int (cfg, GDM_KEY_MAXFILE);

    GdmXdmcp = ve_config_get_bool (cfg, GDM_KEY_XDMCP);
    GdmDispPerHost = ve_config_get_int (cfg, GDM_KEY_DISPERHOST);
    GdmMaxPending = ve_config_get_int (cfg, GDM_KEY_MAXPEND);
    GdmMaxManageWait = ve_config_get_int (cfg, GDM_KEY_MAXWAIT);
    GdmMaxSessions = ve_config_get_int (cfg, GDM_KEY_MAXSESS);
    GdmPort = ve_config_get_int (cfg, GDM_KEY_UDPPORT);
    GdmIndirect = ve_config_get_bool (cfg, GDM_KEY_INDIRECT);
    GdmMaxIndirect = ve_config_get_int (cfg, GDM_KEY_MAXINDIR);
    GdmMaxIndirectWait = ve_config_get_int (cfg, GDM_KEY_MAXINDWAIT);    
    GdmPingInterval = ve_config_get_int (cfg, GDM_KEY_PINGINTERVAL);    
    GdmWilling = ve_config_get_string (cfg, GDM_KEY_WILLING);    

    GdmStandardXServer = ve_config_get_string (cfg, GDM_KEY_STANDARD_XSERVER);    
    if (ve_string_empty (GdmStandardXServer) ||
	access (GdmStandardXServer, X_OK) != 0) {
	    gdm_info (_("%s: Standard X server not found, trying alternatives"),
			"gdm_config_parse");
	    if (access ("/usr/X11R6/bin/X", X_OK) == 0) {
		    g_free (GdmStandardXServer);
		    GdmStandardXServer = g_strdup ("/usr/X11R6/bin/X");
	    } else if (access ("/opt/X11R6/bin/X", X_OK) == 0) {
		    g_free (GdmStandardXServer);
		    GdmStandardXServer = g_strdup ("/opt/X11R6/bin/X");
	    } else if (ve_string_empty (GdmStandardXServer)) {
		    g_free (GdmStandardXServer);
		    GdmStandardXServer = g_strdup ("/usr/bin/X11/X");
	    }
    }
    GdmFlexibleXServers = ve_config_get_int (cfg, GDM_KEY_FLEXIBLE_XSERVERS);    
    GdmXnest = ve_config_get_string (cfg, GDM_KEY_XNEST);    
    if (ve_string_empty (GdmXnest))
	    GdmXnest = NULL;

    GdmFirstVT = ve_config_get_int (cfg, GDM_KEY_FIRSTVT);    
    GdmVTAllocation = ve_config_get_bool (cfg, GDM_KEY_VTALLOCATION);    
    GdmDisallowTCP = ve_config_get_bool (cfg, GDM_KEY_DISALLOWTCP);    

    GdmDebug = ve_config_get_bool (cfg, GDM_KEY_DEBUG);

    /* sanitize some values */

#ifndef HAVE_LIBXDMCP
    if (GdmXdmcp) {
	    gdm_info (_("%s: XDMCP was enabled while there is no XDMCP support, turning it off"), "gdm_config_parse");
	    GdmXdmcp = FALSE;
    }
#endif

    if ( ! GdmAutomaticLoginEnable ||
	ve_string_empty (GdmAutomaticLogin)) {
	    g_free (GdmAutomaticLogin);
	    GdmAutomaticLogin = NULL;
    }

    if (GdmAutomaticLogin != NULL &&
	strcmp (GdmAutomaticLogin, gdm_root_user ()) == 0) {
	    gdm_info (_("%s: Root cannot be autologged in, turing off automatic login"), "gdm_config_parse");
	    g_free (GdmAutomaticLogin);
	    GdmAutomaticLogin = NULL;
    }

    if ( ! GdmTimedLoginEnable ||
	ve_string_empty (GdmTimedLogin)) {
	    g_free (GdmTimedLogin);
	    GdmTimedLogin = NULL;
    }

    if (GdmTimedLogin != NULL &&
	strcmp (GdmTimedLogin, gdm_root_user ()) == 0) {
	    gdm_info (_("%s: Root cannot be autologged in, turing off timed login"), "gdm_config_parse");
	    g_free (GdmTimedLogin);
	    GdmTimedLogin = NULL;
    }

    if (GdmTimedLoginDelay < 5) {
	    gdm_info (_("%s: TimedLoginDelay less than 5, so I will just use 5."), "gdm_config_parse");
	    GdmTimedLoginDelay = 5;
    }

    if (GdmMaxIndirect < 0) {
	    GdmMaxIndirect = 0;
    }

    /* Prerequisites */ 
    if (ve_string_empty (GdmGreeter)) {
	    gdm_error (_("%s: No greeter specified."), "gdm_config_parse");
    }
    if (ve_string_empty (GdmRemoteGreeter)) {
	    gdm_error (_("%s: No remote greeter specified."), "gdm_config_parse");
    }

    if (ve_string_empty (GdmSessDir))
	gdm_error (_("%s: No sessions directory specified."), "gdm_config_parse");

    /* Find server definitions */
    list = ve_config_get_sections (cfg);
    for (li = list; li != NULL; li = li->next) {
	    const char *sec = li->data;
	    if (strncmp (sec, "server-", strlen ("server-")) == 0) {
		    GdmXServer *svr = g_new0 (GdmXServer, 1);
		    char buf[256];

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
			    gdm_error (_("%s: Empty server command, "
					 "using standard one."),
				       "gdm_config_parse");
			    g_free (svr->command);
			    svr->command = g_strdup (GdmStandardXServer);
		    }

		    xservers = g_slist_append (xservers, svr);
	    }
    }
    ve_config_free_list_of_strings (list);

    if (xservers == NULL ||
	gdm_find_x_server (GDM_STANDARD) == NULL) {
	    GdmXServer *svr = g_new0 (GdmXServer, 1);

	    svr->id = g_strdup (GDM_STANDARD);
	    svr->name = g_strdup ("Standard server");
	    svr->command = g_strdup (GdmStandardXServer);
	    svr->flexible = TRUE;
	    svr->choosable = TRUE;
	    svr->handled = TRUE;

	    xservers = g_slist_append (xservers, svr);
    }

    /* Find local X server definitions */
    list = ve_config_get_keys (cfg, GDM_KEY_SERVERS);
    /* only read the list if no_console is FALSE
       at this stage */
    for (li = list; ! no_console && li != NULL; li = li->next) {
	    const char *key = li->data;
	    if (isdigit (*key)) {
		    char *full;
		    char *val;
		    int disp_num = atoi (key);
		    GdmDisplay *disp;

		    while (display_exists (disp_num)) {
			    disp_num++;
		    }

		    if (disp_num != atoi (key)) {
			    gdm_error (_("%s: Display number %d in use!  I will use %d"),
				       "gdm_config_parse", atoi (key), disp_num);
		    }

		    full = g_strdup_printf ("%s/%s", GDM_KEY_SERVERS, key);
		    val = ve_config_get_string (cfg, full);
		    g_free (full);

		    disp = gdm_server_alloc (disp_num, val);
		    g_free (val);

		    if (disp == NULL)
			    continue;
		    displays = g_slist_insert_sorted (displays,
						      disp,
						      compare_displays);
		    if (disp_num > high_display_num)
			    high_display_num = disp_num;
	    } else {
		    gdm_info (_("%s: Invalid server line in config file. Ignoring!"), "gdm_config_parse");
	    }
    }
    ve_config_free_list_of_strings (list);

    if (displays == NULL && ! GdmXdmcp) {
	    char *server = NULL;

	    /* if we requested no local servers (there is no console),
	       then don't display errors in console messages */
	    if (no_console) {
		    gdm_fail (_("%s: XDMCP disabled and no local servers defined. Aborting!"), "gdm_config_parse");
	    }

	    if (access (GdmStandardXServer, X_OK) == 0) {
		    server = GdmStandardXServer;
	    } else if (access ("/usr/bin/X11/X", X_OK) == 0) {
		    server = "/usr/bin/X11/X";
	    } else if (access ("/usr/X11R6/bin/X", X_OK) == 0) {
		    server = "/usr/X11R6/bin/X";
	    } else if (access ("/opt/X11R6/bin/X", X_OK) == 0) {
		    server = "/opt/X11R6/bin/X";
	    }
	    /* yay, we can add a backup emergency server */
	    if (server != NULL) {
		    int num = gdm_get_free_display (0 /* start */,
						    0 /* server uid */);
		    gdm_error (_("%s: XDMCP disabled and no local servers defined. Adding %s on :%d to allow configuration!"),
			       "gdm_config_parse",
			       server, num);

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
		    char *s = g_strdup_printf
			    (_("XDMCP is disabled and gdm "
			       "cannot find any local server "
			       "to start.  Aborting!  Please "
			       "correct the configuration %s "
			       "and restart gdm."),
			     GDM_CONFIG_FILE);
		    gdm_text_message_dialog (s);
		    GdmPidFile = NULL;
		    gdm_fail (_("%s: XDMCP disabled and no local servers defined. Aborting!"), "gdm_config_parse");
	    }
    }

    /* If no displays were found, then obviously
       we're in a no console mode */
    if (displays == NULL)
	    no_console = TRUE;

    /* Lookup user and groupid for the gdm user */
    pwent = getpwnam (GdmUser);

    if (pwent == NULL) {
	    gdm_error (_("%s: Can't find the gdm user (%s). Trying 'nobody'!"), "gdm_config_parse", GdmUser);
	    g_free (GdmUser);
	    GdmUser = g_strdup ("nobody");
	    pwent = getpwnam (GdmUser);
    }

    if (pwent == NULL) {
	    char *s = g_strdup_printf
		    (_("The gdm user does not exist. "
		       "Please correct gdm configuration %s "
		       "and restart gdm."),
		     GDM_CONFIG_FILE);
	    if ( ! no_console)
		    gdm_text_message_dialog (s);
	    GdmPidFile = NULL;
	    gdm_fail (_("%s: Can't find the gdm user (%s). Aborting!"), "gdm_config_parse", GdmUser);
    } else {
	    GdmUserId = pwent->pw_uid;
    }

    if (GdmUserId == 0) {
	    char *s = g_strdup_printf
		    (_("The gdm user is set to be root, but "
		       "this is not allowed since it can "
		       "pose a security risk.  Please "
		       "correct gdm configuration %s and "
		       "restart gdm."), GDM_CONFIG_FILE);
	    if ( ! no_console)
		    gdm_text_message_dialog (s);
	    GdmPidFile = NULL;
	    gdm_fail (_("%s: The gdm user should not be root. Aborting!"), "gdm_config_parse");
    }

    grent = getgrnam (GdmGroup);

    if (grent == NULL) {
	    gdm_error (_("%s: Can't find the gdm group (%s). Trying 'nobody'!"), "gdm_config_parse", GdmUser);
	    g_free (GdmGroup);
	    GdmGroup = g_strdup ("nobody");
	    grent = getgrnam (GdmGroup);
    }

    if (grent == NULL) {
	    char *s = g_strdup_printf
		    (_("The gdm group does not exist. "
		       "Please correct gdm configuration %s "
		       "and restart gdm."),
		     GDM_CONFIG_FILE);
	    if ( ! no_console)
		    gdm_text_message_dialog (s);
	    GdmPidFile = NULL;
	    gdm_fail (_("%s: Can't find the gdm group (%s). Aborting!"), "gdm_config_parse", GdmGroup);
    } else  {
	    GdmGroupId = grent->gr_gid;   
    }

    if (GdmGroupId == 0) {
	    char *s = g_strdup_printf
		    (_("The gdm group is set to be root, but "
		       "this is not allowed since it can "
		       "pose a security risk. Please "
		       "correct gdm configuration %s and "
		       "restart gdm."), GDM_CONFIG_FILE);
	    if ( ! no_console)
		    gdm_text_message_dialog (s);
	    GdmPidFile = NULL;
	    gdm_fail (_("%s: The gdm group should not be root. Aborting!"), "gdm_config_parse");
    }

    /* get the actual commands to use */
    GdmHaltReal = ve_get_first_working_command (GdmHalt, FALSE);
    GdmRebootReal = ve_get_first_working_command (GdmReboot, FALSE);
    GdmSuspendReal = ve_get_first_working_command (GdmSuspend, FALSE);

    setegid (GdmGroupId);	/* gid remains `gdm' */
    seteuid (GdmUserId);

    /* Check that the greeter can be executed */
    bin = ve_first_word (GdmGreeter);
    if (ve_string_empty (bin) ||
	access (bin, X_OK) != 0) {
	    gdm_error (_("%s: Greeter not found or can't be executed by the gdm user"), "gdm_config_parse");
    }
    g_free (bin);

    bin = ve_first_word (GdmRemoteGreeter);
    if (ve_string_empty (bin) ||
	access (bin, X_OK) != 0) {
	    gdm_error (_("%s: Remote greeter not found or can't be executed by the gdm user"), "gdm_config_parse");
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

    /* Check the serv auth and log dirs */
    if (ve_string_empty (GdmServAuthDir)) {
	    if ( ! no_console)
		    gdm_text_message_dialog
			    (_("No daemon/ServAuthDir specified in the configuration file"));
	    GdmPidFile = NULL;
	    gdm_fail (_("%s: No daemon/ServAuthDir specified."), "gdm_config_parse");
    }

    if (ve_string_empty (GdmLogDir)) {
	g_free (GdmLogDir);
	GdmLogDir = g_strdup (GdmServAuthDir);
    }

    /* Enter paranoia mode */
    check_servauthdir (&statbuf);

    seteuid (0);
    setegid (0);

    /* Now set things up for us as  */
    chown (GdmServAuthDir, 0, GdmGroupId);
    chmod (GdmServAuthDir, (S_IRWXU|S_IRWXG|S_ISVTX));

    setegid (GdmGroupId);
    seteuid (GdmUserId);

    /* again paranoid */
    check_servauthdir (&statbuf);

    if (statbuf.st_uid != 0 || statbuf.st_gid != GdmGroupId)  {
	    char *s = g_strdup_printf
		    (_("Server Authorization directory "
		       "(daemon/ServAuthDir) is set to %s "
		       "but is not owned by user %s and group "
		       "%s. Please correct the ownership or "
		       "gdm configuration %s and restart "
		       "gdm."),
		     GdmServAuthDir, GdmUser, GdmGroup,
		     GDM_CONFIG_FILE);
	    if ( ! no_console)
		    gdm_text_message_dialog (s);
	    GdmPidFile = NULL;
	    gdm_fail (_("%s: Authdir %s is not owned by user %s, group %s. Aborting."), "gdm_config_parse", 
		      GdmServAuthDir, gdm_root_user (), GdmGroup);
    }

    if (statbuf.st_mode != (S_IFDIR|S_IRWXU|S_IRWXG|S_ISVTX))  {
	    char *s = g_strdup_printf
		    (_("Server Authorization directory "
		       "(daemon/ServAuthDir) is set to %s "
		       "but has the wrong permissions, it "
		       "should have permissions of %o. "
		       "Please correct the permissions or "
		       "the gdm configuration %s and "
		       "restart gdm."),
		     GdmServAuthDir, (S_IRWXU|S_IRWXG|S_ISVTX), GDM_CONFIG_FILE);
	    if ( ! no_console)
		    gdm_text_message_dialog (s);
	    GdmPidFile = NULL;
	    gdm_fail (_("%s: Authdir %s has wrong permissions %o. Should be %o. Aborting."), "gdm_config_parse", 
		      GdmServAuthDir, statbuf.st_mode, (S_IRWXU|S_IRWXG|S_ISVTX));
    }

    seteuid (0);
    setegid (0);

    check_logdir ();

    /* Check that user authentication is properly configured */
    gdm_verify_check ();

    ve_config_destroy (cfg);
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

	if ((pf = gdm_safe_fopen_w (GdmPidFile)) != NULL) {
	    fprintf (pf, "%d\n", (int)pid);
	    fclose (pf);
	}

        exit (EXIT_SUCCESS);
    }
    gdm_main_pid = getpid ();

    if (pid < 0) 
	gdm_fail (_("%s: fork() failed!"), "gdm_daemonify");

    if (setsid() < 0)
	gdm_fail (_("%s: setsid() failed: %s!"), "gdm_daemonify",
		  strerror(errno));

    IGNORE_EINTR (chdir (GdmServAuthDir));
    umask (022);

    IGNORE_EINTR (close (0));
    IGNORE_EINTR (close (1));
    IGNORE_EINTR (close (2));

    gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
    gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
    gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */
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
				gdm_first_login = FALSE;
				break;
			}
		}
	}
}

void
gdm_final_cleanup (void)
{
	GSList *list, *li;
	gboolean first;

	gdm_debug ("gdm_final_cleanup");

	gdm_in_final_cleanup = TRUE;

	if (extra_process > 1) {
		/* we sigterm extra processes, and we
		 * don't wait */
		kill (-(extra_process), SIGTERM);
		extra_process = 0;
	}

	/* First off whack all XDMCP and FLEXI_XNEST
	   slaves, we'll wait for them later */
	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *d = li->data;
		if (d->type == TYPE_XDMCP ||
		    d->type == TYPE_FLEXI_XNEST) {
			/* set to DEAD so that we won't kill it again */
			d->dispstat = DISPLAY_DEAD;
			if (d->slavepid > 1)
				kill (d->slavepid, SIGTERM);
		}
	}

	/* Now completely unmanage the local servers */
	first = TRUE;
	list = g_slist_copy (displays);
	/* somewhat of a hack to kill last server
	 * started first.  This mostly makes things end up on
	 * the right vt */
	list = g_slist_reverse (list);
	for (li = list; li != NULL; li = li->next) {
		GdmDisplay *d = li->data;
		if (d->type == TYPE_XDMCP ||
		    d->type == TYPE_FLEXI_XNEST)
			continue;
		/* HACK! Wait 2 seconds between killing of local servers
		 * because X is stupid and full of races and will hang my
		 * keyboard if I don't */
		if ( ! first) {
			/* there could be signals happening
			   here */
			gdm_sleep_no_signal (2);
		}
		first = FALSE;
		gdm_display_unmanage (d);
	}
	g_slist_free (list);

	/* and now kill and wait for the XDMCP and FLEXI_XNEST
	   slaves.  unmanage will not kill slaves we have already
	   killed unless a SIGTERM was sent in the meantime */

	list = g_slist_copy (displays);
	for (li = list; li != NULL; li = li->next) {
		GdmDisplay *d = li->data;
		/* XDMCP and FLEXI_XNEST are safe to kill
		 * immediately */
		if (d->type == TYPE_XDMCP ||
		    d->type == TYPE_FLEXI_XNEST)
			gdm_display_unmanage (d);
	}
	g_slist_free (list);


	/* Close stuff */

	if (GdmXdmcp)
		gdm_xdmcp_close ();

	if (fifoconn != NULL) {
		char *path;
		gdm_connection_close (fifoconn);
		path = g_strconcat (GdmServAuthDir, "/.gdmfifo", NULL);
		IGNORE_EINTR (unlink (path));
		g_free (path);
		fifoconn = NULL;
	}

	if (pipeconn != NULL) {
		gdm_connection_close (pipeconn);
		pipeconn = NULL;
	}

	if (slave_fifo_pipe_fd >= 0) {
		IGNORE_EINTR (close (slave_fifo_pipe_fd));
		slave_fifo_pipe_fd = -1;
	}

	if (unixconn != NULL) {
		gdm_connection_close (unixconn);
		IGNORE_EINTR (unlink (GDM_SUP_SOCKET));
		unixconn = NULL;
	}

	closelog();

	if (GdmPidFile != NULL)
		IGNORE_EINTR (unlink (GdmPidFile));
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
		    gdm_info (_("%s: Trying failsafe X "
				"server %s"), 
			      "deal_with_x_crashes",
			      GdmFailsafeXServer);
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

	    gdm_info (_("%s: Running the "
			"XKeepsCrashing script"),
		      "deal_with_x_crashes");

	    extra_process = pid = fork ();
	    if (pid < 0)
		    extra_process = 0;

	    if (pid == 0) {
		    char *argv[2];
		    char *xlog = g_strconcat (GdmLogDir, "/", d->name, ".log", NULL);

		    gdm_unset_signals ();

		    /* Also make a new process group so that we may use
		     * kill -(extra_process) to kill extra process and all it's
		     * possible children */
		    setsid ();

		    if (GdmXdmcp)
			    gdm_xdmcp_close ();

		    closelog ();

		    gdm_close_all_descriptors (0 /* from */, -1 /* except */, -1 /* except2 */);

		    /* No error checking here - if it's messed the best response
		    * is to ignore & try to continue */
		    gdm_open_dev_null (O_RDONLY); /* open stdin - fd 0 */
		    gdm_open_dev_null (O_RDWR); /* open stdout - fd 1 */
		    gdm_open_dev_null (O_RDWR); /* open stderr - fd 2 */

		    argv[0] = GdmXKeepsCrashing;
		    argv[1] = NULL;

		    gdm_restoreenv ();

		    /* unset DISPLAY and XAUTHORITY if they exist
		     * so that gdialog (if used) doesn't get confused */
		    ve_unsetenv ("DISPLAY");
		    ve_unsetenv ("XAUTHORITY");

		    /* some promised variables */
		    ve_setenv ("XLOG", xlog, TRUE);
		    ve_setenv ("BINDIR", EXPANDED_BINDIR, TRUE);
		    ve_setenv ("SBINDIR", EXPANDED_SBINDIR, TRUE);
		    ve_setenv ("LIBEXECDIR", EXPANDED_LIBEXECDIR, TRUE);
		    ve_setenv ("SYSCONFDIR", EXPANDED_SYSCONFDIR, TRUE);

		    /* To enable gettext stuff in the script */
		    ve_setenv ("TEXTDOMAIN", GETTEXT_PACKAGE, TRUE);
		    ve_setenv ("TEXTDOMAINDIR", GNOMELOCALEDIR, TRUE);

		    IGNORE_EINTR (execv (argv[0], argv));
	
		    /* yaikes! */
		    _exit (32);
	    } else if (pid > 0) {
		    int status;

		    if (extra_process > 1) {
			    int ret;
			    int killsignal = SIGTERM;
			    int storeerrno;
			    errno = 0;
			    ret = waitpid (extra_process, &status, WNOHANG);
			    do {
				    /* wait for some signal, yes this is a race */
				    if (ret <= 0)
					    sleep (10);
				    errno = 0;
				    ret = waitpid (extra_process, &status, WNOHANG);
				    storeerrno = errno;
				    if (ret <= 0 &&
					(ve_signal_was_notified (SIGTERM) ||
					 ve_signal_was_notified (SIGINT) ||
					 ve_signal_was_notified (SIGHUP))) {
					    kill (-(extra_process), killsignal);
					    killsignal = SIGKILL;
				    }
			    } while (ret == 0 || (ret < 0 && storeerrno == EINTR));
		    }
		    extra_process = 0;

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
	access (EXPANDED_LIBEXECDIR "/gdmopen", X_OK) == 0) {
	    /* Shit if we knew what the program was to tell the user,
	     * the above script would have been defined and we'd run
	     * it for them */
	    const char *error =
		    _("I cannot start the X server (your graphical "
		      "interface).  It is likely that it is not set "
		      "up correctly. You will need to log in on a "
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

static void
suspend_machine (void)
{
	if (GdmSuspendReal != NULL &&
	    fork () == 0) {
		char **argv;
		if (GdmXdmcp)
			gdm_xdmcp_close ();
		/* In the child setup empty mask and set all signals to
		 * default values */
		gdm_unset_signals ();

		/* Also make a new process group */
		setsid ();

		IGNORE_EINTR (chdir ("/"));

		argv = ve_split (GdmSuspendReal);
		IGNORE_EINTR (execv (argv[0], argv));
		/* FIXME: what about fail */
		_exit (1);
	}
}

#ifdef __linux__
static void
change_to_first_and_clear (gboolean reboot)
{
	gdm_change_vt (1);
	IGNORE_EINTR (close (1));
	IGNORE_EINTR (close (2));
	open ("/dev/tty1", O_WRONLY);
	open ("/dev/tty1", O_WRONLY);
	/* yet again lazy */
	if (access ("/usr/bin/clear", X_OK) == 0)
		system ("/usr/bin/clear");
	else if (access ("/bin/clear", X_OK) == 0)
		system ("/bin/clear");
	printf ("\n\n\n");
	if (reboot)
		printf (_("System is rebooting, please wait ..."));
	else
		printf (_("System is shutting down, please wait ..."));
	printf ("\n\n\n");
}
#endif /* __linux__ */


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
	    gdm_debug ("gdm_cleanup_children: child %d returned %d", pid, status);
    } else {
	    status = EXIT_SUCCESS;
	    crashed = TRUE;
	    if (WIFSIGNALED (exitstatus)) {
		    if (WTERMSIG (exitstatus) == SIGTERM ||
			WTERMSIG (exitstatus) == SIGINT) {
			    /* we send these signals, sometimes children don't handle them */
			    gdm_debug ("gdm_cleanup_children: child %d died of signal %d (TERM/INT)", pid,
				       (int)WTERMSIG (exitstatus));
		    } else {
			    gdm_error ("gdm_cleanup_children: child %d crashed of signal %d", pid,
				       (int)WTERMSIG (exitstatus));
		    }
	    } else {
		    gdm_error ("gdm_cleanup_children: child %d crashed", pid);
	    }
    }
	
    if (pid == extra_process) {
	    /* an extra process died, yay! */
	    extra_process = 0;
	    extra_status = exitstatus;
	    return TRUE;
    }

    /* Find out who this slave belongs to */
    d = gdm_display_lookup (pid);

    if (d == NULL)
	    return TRUE;

    if (crashed) {
	    gdm_error ("gdm_cleanup_children: Slave crashed, killing its "
		       "children");

	    if (d->sesspid > 1)
		    kill (-(d->sesspid), SIGTERM);
	    d->sesspid = 0;
	    if (d->greetpid > 1)
		    kill (-(d->greetpid), SIGTERM);
	    d->greetpid = 0;
	    if (d->chooserpid > 1)
		    kill (-(d->chooserpid), SIGTERM);
	    d->chooserpid = 0;
	    if (d->servpid > 1)
		    kill (d->servpid, SIGTERM);
	    d->servpid = 0;

	    /* race avoider */
	    gdm_sleep_no_signal (1);
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
	 status == DISPLAY_SUSPEND ||
	 status == DISPLAY_HALT)) {
	    gdm_info (_("Reboot or Halt request when there is no system menu from display %s"), d->name);
	    status = DISPLAY_REMANAGE;
    }

    if ( ! d->console &&
	(status == DISPLAY_RESTARTGDM ||
	 status == DISPLAY_REBOOT ||
	 status == DISPLAY_SUSPEND ||
	 status == DISPLAY_HALT)) {
	    gdm_info (_("Restart, Reboot or Halt request from a non-local display %s"), d->name);
	    status = DISPLAY_REMANAGE;
    }

    if (status == DISPLAY_RUN_CHOOSER) {
	    /* use the chooser on the next run (but only if allowed) */
	    if (GdmSystemMenu &&
		GdmChooserButton)
		    d->use_chooser = TRUE;
	    status = DISPLAY_REMANAGE;
	    /* go around the display loop detection, these are short
	     * sessions, so this decreases the chances of the loop
	     * detection being hit */
	    d->last_loop_start_time = 0;
    }

    if (status == DISPLAY_CHOSEN) {
	    /* forget about this indirect id, since this
	     * display will be dead very soon, and we don't want it
	     * to take the indirect display with it */
	    d->indirect_id = 0;
	    status = DISPLAY_REMANAGE;
    }

    if (status == DISPLAY_GREETERFAILED) {
	    if (d->managetime + 10 >= time (NULL)) {
		    d->try_different_greeter = TRUE;
	    } else {
		    d->try_different_greeter = FALSE;
	    }
	    /* now just remanage */
	    status = DISPLAY_REMANAGE;
    } else {
	    d->try_different_greeter = FALSE;
    }

    /* checkout if we can actually do stuff */
    switch (status) {
    case DISPLAY_REBOOT:
	    if (GdmRebootReal == NULL)
		    status = DISPLAY_REMANAGE;
	    break;
    case DISPLAY_HALT:
	    if (GdmHaltReal == NULL)
		    status = DISPLAY_REMANAGE;
	    break;
    case DISPLAY_SUSPEND:
	    if (GdmSuspendReal == NULL)
		    status = DISPLAY_REMANAGE;
	    break;
    default:
	    break;
    }

start_autopsy:

    /* Autopsy */
    switch (status) {
	
    case DISPLAY_ABORT:		/* Bury this display for good */
	gdm_info (_("%s: Aborting display %s"),
		  "gdm_child_action", d->name);

	if (gdm_restart_mode)
		gdm_safe_restart ();

	gdm_display_unmanage (d);

	/* If there are some pending locals, start them now */
	gdm_start_first_unborn_local (3 /* delay */);
	break;
	
    case DISPLAY_REBOOT:	/* Reboot machine */
	gdm_info (_("Master rebooting..."));

	gdm_final_cleanup ();
	IGNORE_EINTR (chdir ("/"));

#ifdef __linux__
	change_to_first_and_clear (TRUE /* reboot */);
#endif /* __linux */

	argv = ve_split (GdmRebootReal);
	IGNORE_EINTR (execv (argv[0], argv));

	gdm_error (_("%s: Reboot failed: %s"), 
		   "gdm_child_action", strerror (errno));

	status = DISPLAY_REMANAGE;
	goto start_autopsy;
	break;
	
    case DISPLAY_HALT:		/* Halt machine */
	gdm_info (_("Master halting..."));

	gdm_final_cleanup ();
	IGNORE_EINTR (chdir ("/"));

#ifdef __linux__
	change_to_first_and_clear (FALSE /* reboot */);
#endif /* __linux */

	argv = ve_split (GdmHaltReal);
	IGNORE_EINTR (execv (argv[0], argv));

	gdm_error (_("%s: Halt failed: %s"),
		   "gdm_child_action", strerror (errno));

	status = DISPLAY_REMANAGE;
	goto start_autopsy;
	break;

    case DISPLAY_SUSPEND:	/* Suspend machine */
	/* XXX: this is ugly, why should there be a suspend like this,
	 * see GDM_SOP_SUSPEND_MACHINE */
	gdm_info (_("Master suspending..."));
	suspend_machine ();

	status = DISPLAY_REMANAGE;
	goto start_autopsy;
	break;

    case DISPLAY_RESTARTGDM:
	gdm_restart_now ();
	break;

    case DISPLAY_XFAILED:       /* X sucks */
	gdm_debug ("X failed!");
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
		/* this is a much faster failing, don't even allow a few
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
		d->last_loop_start_time = 0;
	}
	/* fall through */

    case DISPLAY_REMANAGE:	/* Remanage display */
    default:
	gdm_debug ("gdm_child_action: In remanage");

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
	} else if (d->type == TYPE_FLEXI || d->type == TYPE_FLEXI_XNEST) {
		/* if this was a chooser session and we have chosen a host,
		   then we don't want to unmanage, we want to manage and
		   choose that host */
		if (d->chosen_hostname != NULL) {
			if ( ! gdm_display_manage (d)) {
				gdm_display_unmanage (d);
			}
		} else {
			/* else, this is a one time thing */
			gdm_display_unmanage (d);
		}
	/* Remote displays will send a request to be managed */
	} else /* TYPE_XDMCP */ {
		gdm_display_unmanage (d);
	}
	
	break;
    }

    if (gdm_restart_mode)
	    gdm_safe_restart ();

    return TRUE;
}

static void
gdm_restart_now (void)
{
	gdm_info (_("GDM restarting ..."));
	gdm_final_cleanup ();
	gdm_restoreenv ();
	IGNORE_EINTR (execvp (stored_argv[0], stored_argv));
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

static void
main_daemon_abrt (int sig)
{
	/* FIXME: note that this could mean out of memory */
	gdm_error ("main daemon: Got SIGABRT, something went very wrong. Going down!");
	gdm_final_cleanup ();
	exit (EXIT_FAILURE);
}

static gboolean
mainloop_sig_callback (int sig, gpointer data)
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
      gdm_final_cleanup ();
      exit (EXIT_SUCCESS);
      break;

#ifdef SIGXFSZ
    case SIGXFSZ:
      gdm_error ("main daemon: Hit file size rlimit, restarting!");
      gdm_restart_now ();
      break;
#endif

#ifdef SIGXCPU
    case SIGXCPU:
      gdm_error ("main daemon: Hit CPU rlimit, restarting!");
      gdm_restart_now ();
      break;
#endif

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

/*
 * main: The main daemon control
 */

static void
store_argv (int argc, char *argv[])
{
	int i;

	stored_argv = g_new0 (char *, argc + 1);
	for (i = 0; i < argc; i++)
		stored_argv[i] = g_strdup (argv[i]);
	stored_argv[i] = NULL;
	stored_argc = argc;
}

static void
close_notify (gpointer data)
{
	GdmConnection **conn = data;
	* conn = NULL;
}

static void
create_connections (void)
{
	int p[2];
	gchar *path;

	path = g_strconcat (GdmServAuthDir, "/.gdmfifo", NULL);
	fifoconn = gdm_connection_open_fifo (path, 0660);
	g_free (path);

	if (fifoconn != NULL) {
		gdm_connection_set_handler (fifoconn,
					    gdm_handle_message,
					    NULL /* data */,
					    NULL /* destroy_notify */);
		gdm_connection_set_close_notify (fifoconn,
						 &fifoconn,
						 close_notify);
	}

	if (pipe (p) < 0) {
		slave_fifo_pipe_fd = -1;
		pipeconn = NULL;
	} else {
		slave_fifo_pipe_fd = p[1];
		pipeconn = gdm_connection_open_fd (p[0]);
	}

	if (pipeconn != NULL) {
		gdm_connection_set_handler (pipeconn,
					    gdm_handle_message,
					    NULL /* data */,
					    NULL /* destroy_notify */);
		gdm_connection_set_close_notify (pipeconn,
						 &pipeconn,
						 close_notify);
	} else {
		IGNORE_EINTR (close (p[0]));
		IGNORE_EINTR (close (p[1]));
		slave_fifo_pipe_fd = -1;
	}


	unixconn = gdm_connection_open_unix (GDM_SUP_SOCKET, 0666);

	if (unixconn != NULL) {
		gdm_connection_set_handler (unixconn,
					    gdm_handle_user_message,
					    NULL /* data */,
					    NULL /* destroy_notify */);
		gdm_connection_set_close_notify (unixconn,
						 &unixconn,
						 close_notify);
	}
}

struct poptOption options [] = {
	{ "nodaemon", '\0', POPT_ARG_NONE | POPT_ARGFLAG_ONEDASH,
	  &no_daemon, 0, N_("Do not fork into the background"), NULL },
	{ "no-console", '\0', POPT_ARG_NONE,
	  &no_console, 0, N_("No console (local) servers to be run"), NULL },
	{ "preserve-ld-vars", '\0', POPT_ARG_NONE,
	  &preserve_ld_vars, 0, N_("Preserve LD_* variables"), NULL },
        POPT_AUTOHELP
	{ NULL, 0, 0, NULL, 0}
};

static gboolean
linux_only_is_running (pid_t pid)
{
	char resolved_self[PATH_MAX];
	char resolved_running[PATH_MAX];

	char *running = g_strdup_printf ("/proc/%lu/exe", (gulong)pid);

	if (realpath ("/proc/self/exe", resolved_self) == NULL) {
		g_free (running);
		/* probably not a linux system */
		return TRUE;
	}

	if (realpath (running, resolved_running) == NULL) {
		g_free (running);
		/* probably not a linux system */
		return TRUE;
	}

	g_free (running);

	if (strcmp (resolved_running, resolved_self) == 0)
		return TRUE;
	return FALSE;
}

static void
ensure_desc_012 (void)
{
	int fd;
	/* We here ensure descriptors 0, 1 and 2
	 * we of course count on the fact that open
	 * opens the lowest available descriptor */
	for (;;) {
		fd = gdm_open_dev_null (O_RDWR);
		/* Once we are up to 3, we're beyond stdin,
		 * stdout and stderr */
		if (fd >= 3) {
			IGNORE_EINTR (close (fd));
			break;
		}
	}
}

static void
gdm_get_our_runlevel (void)
{
#ifdef __linux__
	/* on linux we get our current runlevel, for use later
	 * to detect a shutdown going on, and not mess up. */
	if (access ("/sbin/runlevel", X_OK) == 0) {
		char ign;
		int rnl;
		FILE *fp = fopen ("/sbin/runlevel", "r");
		if (fscanf (fp, "%c %d", &ign, &rnl) == 2) {
			gdm_normal_runlevel = rnl;
		}
		fclose (fp);
	}
#endif /* __linux__ */
}

/* initially if we get a TERM or INT we just want to die,
   but we want to also kill an extra process if it exists */
static void
initial_term_int (int signal)
{
	if (extra_process > 1)
		kill (-(extra_process), SIGTERM);
	_exit (EXIT_FAILURE);
}

int 
main (int argc, char *argv[])
{
    sigset_t mask;
    struct sigaction sig, child, abrt;
    FILE *pf;
    poptContext ctx;
    int nextopt;
    const char *charset;

    gdm_main_pid = getpid ();

    /* We here ensure descriptors 0, 1 and 2 */
    ensure_desc_012 ();

    /* store all initial stuff, args, env, rlimits, runlevel */
    store_argv (argc, argv);
    gdm_saveenv ();
    gdm_get_initial_limits ();
    gdm_get_our_runlevel ();

    bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
    textdomain (GETTEXT_PACKAGE);

    setlocale (LC_ALL, "");

    /* Initialize runtime environment */
    umask (022);

    ctx = poptGetContext ("gdm", argc, (const char **) argv,
			  options, 0);
    while ((nextopt = poptGetNextOpt (ctx)) > 0 || nextopt == POPT_ERROR_BADOPT)
	/* do nothing */ ;

    if (nextopt != -1) {
	    fprintf (stderr,
		     _("Error on option %s: %s.\nRun '%s --help' to see a full list of available command line options.\n"),
		     poptBadOption (ctx, 0),
		     poptStrerror (nextopt),
		     argv[0]);
	    fflush (stdout);
	    exit (1);
    }

    /* XDM compliant error message */
    if (getuid () != 0) {
	    /* make sure the pid file doesn't get wiped */
	    GdmPidFile = NULL;
	    gdm_fail (_("Only root wants to run gdm\n"));
    }

    main_loop = g_main_loop_new (NULL, FALSE);
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    if ( ! g_get_charset (&charset)) {
	    gdm_charset = g_strdup (charset);
    }

    /* initial TERM/INT handler */
    sig.sa_handler = initial_term_int;
    sig.sa_flags = SA_RESTART;
    sigemptyset (&sig.sa_mask);

    if (sigaction (SIGTERM, &sig, NULL) < 0) 
	gdm_fail (_("%s: Error setting up %s signal handler: %s"),
		  "main", "TERM", strerror (errno));

    if (sigaction (SIGINT, &sig, NULL) < 0) 
	gdm_fail (_("%s: Error setting up %s signal handler: %s"),
		  "main", "INT", strerror (errno));

    /* get the name of the root user */
    gdm_root_user ();

    /* Parse configuration file */
    gdm_config_parse();

    /* Check if another gdm process is already running */
    if (access (GdmPidFile, R_OK) == 0) {

        /* Check if the existing process is still alive. */
        gint pidv;

        pf = fopen (GdmPidFile, "r");

        if (pf != NULL &&
	    fscanf (pf, "%d", &pidv) == 1 &&
	    kill (pidv, 0) == 0 &&
	    linux_only_is_running (pidv)) {
		/* make sure the pid file doesn't get wiped */
		GdmPidFile = NULL;
		fclose (pf);
		gdm_fail (_("gdm already running. Aborting!"));
	}

	if (pf != NULL)
		fclose (pf);
    }

    /* Become daemon unless started in -nodaemon mode or child of init */
    if (no_daemon || getppid() == 1) {

	/* Write pid to pidfile */
	if ((pf = gdm_safe_fopen_w (GdmPidFile)) != NULL) {
	    fprintf (pf, "%d\n", (int)getpid());
	    fclose (pf);
	}

	IGNORE_EINTR (chdir (GdmServAuthDir));
	umask (022);
    }
    else
	gdm_daemonify();

    /* Signal handling */
    ve_signal_add (SIGCHLD, mainloop_sig_callback, NULL);
    ve_signal_add (SIGTERM, mainloop_sig_callback, NULL);
    ve_signal_add (SIGINT, mainloop_sig_callback, NULL);
    ve_signal_add (SIGHUP, mainloop_sig_callback, NULL);
    ve_signal_add (SIGUSR1, mainloop_sig_callback, NULL);

    sig.sa_handler = ve_signal_notify;
    sig.sa_flags = SA_RESTART;
    sigemptyset (&sig.sa_mask);

    if (sigaction (SIGTERM, &sig, NULL) < 0) 
	gdm_fail (_("%s: Error setting up %s signal handler: %s"),
		  "main", "TERM", strerror (errno));

    if (sigaction (SIGINT, &sig, NULL) < 0) 
	gdm_fail (_("%s: Error setting up %s signal handler: %s"),
		  "main", "INT", strerror (errno));

    if (sigaction (SIGHUP, &sig, NULL) < 0) 
	gdm_fail (_("%s: Error setting up %s signal handler: %s"),
		  "main", "HUP", strerror (errno));

    if (sigaction (SIGUSR1, &sig, NULL) < 0) 
	gdm_fail (_("%s: Error setting up %s signal handler: %s"),
		  "main", "USR1", strerror (errno));

    /* some process limit signals we catch and restart on,
       note that we don't catch these in the slave, but then
       we catch those in the main daemon as slave crashing
       (terminated by signal), and we clean up appropriately */
#ifdef SIGXCPU
    ve_signal_add (SIGXCPU, mainloop_sig_callback, NULL);
    if (sigaction (SIGXCPU, &sig, NULL) < 0) 
	gdm_fail (_("%s: Error setting up %s signal handler: %s"),
		  "main", "XCPU", strerror (errno));
#endif
#ifdef SIGXFSZ
    ve_signal_add (SIGXFSZ, mainloop_sig_callback, NULL);
    if (sigaction (SIGXFSZ, &sig, NULL) < 0) 
	gdm_fail (_("%s: Error setting up %s signal handler: %s"),
		  "main", "XFSZ", strerror (errno));
#endif

    /* cannot use mainloop for SIGABRT, the handler can never
       return */
    abrt.sa_handler = main_daemon_abrt;
    abrt.sa_flags = SA_RESTART;
    sigemptyset (&abrt.sa_mask);

    if (sigaction (SIGABRT, &abrt, NULL) < 0) 
	gdm_fail (_("%s: Error setting up %s signal handler: %s"),
		  "main", "ABRT", strerror (errno));

    child.sa_handler = ve_signal_notify;
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
    sigaddset (&mask, SIGABRT);
#ifdef SIGXCPU
    sigaddset (&mask, SIGXCPU);
#endif
#ifdef SIGXFSZ
    sigaddset (&mask, SIGXFSZ);
#endif
    sigprocmask (SIG_UNBLOCK, &mask, NULL);

    gdm_signal_ignore (SIGUSR2);

    /* ignore power failures, up to user processes to
     * handle things correctly */
#ifdef SIGPWR
    gdm_signal_ignore (SIGPWR);
#endif
    /* can we ever even get this one? */
#ifdef SIGLOST
    gdm_signal_ignore (SIGLOST);
#endif

    gdm_debug ("gdm_main: Here we go...");

    /* Init XDMCP if applicable */
    if (GdmXdmcp)
	gdm_xdmcp_init();

    create_connections ();

    /* make sure things (currently /tmp/.ICE-unix) are sane */
    gdm_ensure_sanity () ;

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
	g_main_loop_run (main_loop);
	gdm_debug ("main: Exited main loop");
      }

    return EXIT_SUCCESS;	/* Not reached */
}

static gboolean
order_exists (int order)
{
	GSList *li;
	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *d = li->data;
		if (d->x_servers_order == order)
			return TRUE;
	}
	return FALSE;
}

static int
get_new_order (GdmDisplay *d)
{
	int order;
	GSList *li;
	/* first try the position in the 'displays' list as
	 * our order */
	for (order = 0, li = displays; li != NULL; order++, li = li->next) {
		if (li->data == d)
			break;
	}
	/* next make sure it's unique */
	while (order_exists (order))
		order++;
	return order;
}

static void
write_x_servers (GdmDisplay *d)
{
	FILE *fp;
	char *file = g_strconcat (GdmServAuthDir, "/", d->name, ".Xservers", NULL);
	int i;
	int bogusname;

	if (d->x_servers_order < 0)
		d->x_servers_order = get_new_order (d);

	fp = gdm_safe_fopen_w (file);
	if (fp == NULL) {
		gdm_error ("Can't open %s for writing", file);
		g_free (file);
		return;
	}

	for (bogusname = 0, i = 0; i < d->x_servers_order; bogusname++, i++) {
		char buf[32];
		g_snprintf (buf, sizeof (buf), ":%d", bogusname);
		if (strcmp (buf, d->name) == 0)
			g_snprintf (buf, sizeof (buf), ":%d", ++bogusname);
		fprintf (fp, "%s local /usr/X11R6/bin/Xbogus\n", buf);
	}

	if (d->type == TYPE_XDMCP) {
		fprintf (fp, "%s foreign\n", d->name);
	} else {
		char **argv;
		char *command;
		argv = gdm_server_resolve_command_line
			(d, FALSE /* resolve_flags */,
			 NULL /* vtarg */);
		command = g_strjoinv (" ", argv);
		g_strfreev (argv);
		fprintf (fp, "%s local %s\n", d->name, command);
		g_free (command);
	}

	fclose (fp);
}

static void
send_slave_ack (GdmDisplay *d, const char *resp)
{
	if (d->master_notify_fd >= 0) {
		if (resp == NULL) {
			char not[2];
			not[0] = GDM_SLAVE_NOTIFY_ACK;
			not[1] = '\n';
			IGNORE_EINTR (write (d->master_notify_fd, not, 2));
		} else {
			char *not = g_strdup_printf ("%c%s\n",
						     GDM_SLAVE_NOTIFY_ACK,
						     resp);
			IGNORE_EINTR (write (d->master_notify_fd, not, strlen (not)));
			g_free (not);
		}
	}
	if (d->slavepid > 1) {
		kill (d->slavepid, SIGUSR2);
		/* now yield the CPU as the other process has more
		   useful work to do then we do */
#if defined(_POSIX_PRIORITY_SCHEDULING) && defined(HAVE_SCHED_YIELD)
		sched_yield ();
#endif
	}
}

static void
send_slave_command (GdmDisplay *d, const char *command)
{
	if (d->master_notify_fd >= 0) {
		char *cmd = g_strdup_printf ("%c%s\n",
					     GDM_SLAVE_NOTIFY_COMMAND,
					     command);
		IGNORE_EINTR (write (d->master_notify_fd, cmd, strlen (cmd)));
		g_free (cmd);
	}
	if (d->slavepid > 1) {
		kill (d->slavepid, SIGUSR2);
		/* now yield the CPU as the other process has more
		   useful work to do then we do */
#if defined(_POSIX_PRIORITY_SCHEDULING) && defined(HAVE_SCHED_YIELD)
		sched_yield ();
#endif
	}
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
			gdm_debug ("Handling message: '%s...'", s);
			g_free (s);
		} else if (strncmp (msg, GDM_SOP_SYSLOG " ",
				    strlen (GDM_SOP_SYSLOG " ")) != 0) {
			/* Don't print out the syslog message as it will
			 * be printed out anyway as that's the whole point
			 * of the message. */
			gdm_debug ("Handling message: '%s'", msg);
		}
	}

	if (strncmp (msg, GDM_SOP_CHOSEN " ",
		     strlen (GDM_SOP_CHOSEN " ")) == 0) {
		gdm_choose_data (msg);
	} else if (strncmp (msg, GDM_SOP_CHOSEN_LOCAL " ",
		            strlen (GDM_SOP_CHOSEN_LOCAL " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;
		char *p;

		if (sscanf (msg, GDM_SOP_CHOSEN_LOCAL " %ld", &slave_pid)
		    != 1)
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
			g_free (d->chosen_hostname);
			d->chosen_hostname = g_strdup (p);
			gdm_debug ("Got CHOSEN_LOCAL == %s", p);
			/* send ack */
			send_slave_ack (d, NULL);
		}
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
			send_slave_ack (d, NULL);
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
			send_slave_ack (d, NULL);
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
			send_slave_ack (d, NULL);
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
			send_slave_ack (d, NULL);
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
			send_slave_ack (d, NULL);
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
			send_slave_ack (d, NULL);
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
			d->vt = vt_num;
			gdm_debug ("Got VT_NUM == %d", vt_num);
			/* send ack */
			send_slave_ack (d, NULL);
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
			send_slave_ack (d, NULL);
		}
	} else if (strncmp (msg, GDM_SOP_QUERYLOGIN " ",
		            strlen (GDM_SOP_QUERYLOGIN " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;
		char *p;

		if (sscanf (msg, GDM_SOP_QUERYLOGIN " %ld",
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
			GString *resp = NULL;
			GSList *li;
			gdm_debug ("Got QUERYLOGIN %s", p);
			for (li = displays; li != NULL; li = li->next) {
				GdmDisplay *di = li->data;
				if (di->login != NULL &&
				    strcmp (di->login, p) == 0) {
					if (resp == NULL) {
						resp = g_string_new (di->name);
						g_string_append_printf (resp, ",%d", di->vt);
					} else {
						g_string_append_printf (resp, ",%s,%d",
									di->name, di->vt);
					}
				}
			}

			/* send ack */
			if (resp != NULL) {
				send_slave_ack (d, resp->str);
				g_string_free (resp, TRUE);
			} else {
				send_slave_ack (d, NULL);
			}
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
			send_slave_ack (d, NULL);
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
			send_slave_ack (d, NULL);
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
			send_slave_ack (d, NULL);
		}
	} else if (strcmp (msg, GDM_SOP_SOFT_RESTART) == 0) {
		gdm_restart_mode = TRUE;
		gdm_safe_restart ();
	} else if (strcmp (msg, GDM_SOP_DIRTY_SERVERS) == 0) {
		GSList *li;
		for (li = displays; li != NULL; li = li->next) {
			GdmDisplay *d = li->data;
			send_slave_command (d, GDM_NOTIFY_DIRTY_SERVERS);
		}
	} else if (strcmp (msg, GDM_SOP_SOFT_RESTART_SERVERS) == 0) {
		GSList *li;
		for (li = displays; li != NULL; li = li->next) {
			GdmDisplay *d = li->data;
			send_slave_command (d, GDM_NOTIFY_SOFT_RESTART_SERVERS);
		}
	} else if (strncmp (msg, GDM_SOP_SYSLOG " ",
		            strlen (GDM_SOP_SYSLOG " ")) == 0) {
		char *p;
		long pid;
	       	int type;
		p = strchr (msg, ' ');
		if (p == NULL)
			return;
		p++;
		if (sscanf (p, "%ld", &pid) != 1)
			return;
		p = strchr (p, ' ');
		if (p == NULL)
			return;
		p++;
		if (sscanf (p, "%d", &type) != 1)
			return;

		p = strchr (p, ' ');
		if (p == NULL)
			return;
		p++;

		syslog (type, "(child %ld) %s", pid, p);
	} else if (strcmp (msg, GDM_SOP_START_NEXT_LOCAL) == 0) {
		gdm_start_first_unborn_local (3 /* delay */);
	} else if (strcmp (msg, GDM_SOP_HUP_ALL_GREETERS) == 0) {
		/* probably shouldn't be done too often */
		GSList *li;
		for (li = displays; li != NULL; li = li->next) {
			GdmDisplay *d = li->data;
			if (d->greetpid > 1)
				kill (d->greetpid, SIGHUP);
			else if (d->chooserpid > 1)
				kill (d->chooserpid, SIGHUP);
		}
	} else if (strncmp (msg, GDM_SOP_WRITE_X_SERVERS " ",
		            strlen (GDM_SOP_WRITE_X_SERVERS " ")) == 0) {
		GdmDisplay *d;
		long slave_pid;

		if (sscanf (msg, GDM_SOP_WRITE_X_SERVERS " %ld",
			    &slave_pid) != 1)
			return;

		/* Find out who this slave belongs to */
		d = gdm_display_lookup (slave_pid);

		if (d != NULL) {
			write_x_servers (d);

			/* send ack */
			send_slave_ack (d, NULL);
		}
	} else if (strcmp (msg, GDM_SOP_SUSPEND_MACHINE) == 0) {
		gdm_info (_("Master suspending..."));
		if (GdmSuspendReal != NULL &&
		    GdmSystemMenu) {
			suspend_machine ();
		}

	}
}

/* extract second word and the rest of the string */
static void
extract_dispname_uid_xauthfile_cookie (const char *msg,
				       char **dispname,
				       uid_t *uid,
				       char **xauthfile,
				       char **cookie)
{
	const char *p;
	int i;
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

	/* Get uid */
	p = strchr (p, ' ');
	if (p == NULL) {
		*dispname = NULL;
		g_free (*dispname);
		return;
	}
	while (*p == ' ')
		p++;

	if (sscanf (p, "%d", &i) != 1) {
		*dispname = NULL;
		g_free (*dispname);
		return;
	}
	*uid = i;

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
		unsigned int num;
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
		     gboolean handled,
		     gboolean chooser,
		     const char *xnest_disp, uid_t xnest_uid,
		     const char *xnest_auth_file,
		     const char *xnest_cookie)
{
	GdmDisplay *display;
	char *bin;
	uid_t server_uid = 0;

	gdm_debug ("server: '%s'", server);

	if (type == TYPE_FLEXI_XNEST) {
		struct stat s;
		int r;
		gboolean authorized = TRUE;

		seteuid (xnest_uid);

		g_assert (xnest_auth_file != NULL);
		g_assert (xnest_disp != NULL);
		g_assert (xnest_cookie != NULL);

		IGNORE_EINTR (r = stat (xnest_auth_file, &s));
		if (r < 0)
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

		if (s.st_uid != xnest_uid)
			authorized = FALSE;

		seteuid (0);

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

	/* It is kind of ugly that we don't use
	   the standard resolution for this, but
	   oh well, this makes other things simpler */
	display->handled = handled;
	display->use_chooser = chooser;

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

static gboolean
is_key (const char *key1, const char *key2)
{
	char *key1d, *key2d, *p;

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

static void
notify_displays_int (const char *key, int val)
{
	GSList *li;
	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *disp = li->data;
		if (disp->master_notify_fd >= 0) {
			gdm_fdprintf (disp->master_notify_fd,
				      "%c%s %d\n",
				      GDM_SLAVE_NOTIFY_KEY, key, val);
			if (disp->slavepid > 1)
				kill (disp->slavepid, SIGUSR2);
		}
	}
}

static void
notify_displays_string (const char *key, const char *val)
{
	GSList *li;
	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *disp = li->data;
		if (disp->master_notify_fd >= 0) {
			gdm_fdprintf (disp->master_notify_fd,
				      "%c%s %s\n",
				      GDM_SLAVE_NOTIFY_KEY, key, val);
			if (disp->slavepid > 1)
				kill (disp->slavepid, SIGUSR2);
		}
	}
}

static gboolean
update_config (const char *key)
{
	struct stat statbuf;
	VeConfig *cfg;
	int r;

	IGNORE_EINTR (r = stat (GDM_CONFIG_FILE, &statbuf));
	if (r < 0) {
		/* if the file didn't exist before either */
		if (config_file_mtime == 0)
			return TRUE;
	} else {
		/* if the file didn't exist before either */
		if (config_file_mtime == statbuf.st_mtime)
			return TRUE;
		config_file_mtime = statbuf.st_mtime;
	}

	cfg = ve_config_new (GDM_CONFIG_FILE);

	if (is_key (key, GDM_KEY_ALLOWROOT)) {
		gboolean val = ve_config_get_bool (cfg, GDM_KEY_ALLOWROOT);
		if (ve_bool_equal (val, GdmAllowRoot))
			goto update_config_ok;
		GdmAllowRoot = val;

		notify_displays_int (GDM_NOTIFY_ALLOWROOT, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_ALLOWREMOTEROOT)) {
		gboolean val = ve_config_get_bool (cfg, GDM_KEY_ALLOWREMOTEROOT);
		if (ve_bool_equal (val, GdmAllowRemoteRoot))
			goto update_config_ok;
		GdmAllowRemoteRoot = val;

		notify_displays_int (GDM_NOTIFY_ALLOWREMOTEROOT, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_ALLOWREMOTEAUTOLOGIN)) {
		gboolean val = ve_config_get_bool (cfg, GDM_KEY_ALLOWREMOTEAUTOLOGIN);
		if (ve_bool_equal (val, GdmAllowRemoteAutoLogin))
			goto update_config_ok;
		GdmAllowRemoteAutoLogin = val;

		notify_displays_int (GDM_NOTIFY_ALLOWREMOTEAUTOLOGIN, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_SYSMENU)) {
		gboolean val = ve_config_get_bool (cfg, GDM_KEY_SYSMENU);
		if (ve_bool_equal (val, GdmSystemMenu))
			goto update_config_ok;
		GdmSystemMenu = val;

		notify_displays_int (GDM_NOTIFY_SYSMENU, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_CONFIG_AVAILABLE)) {
		gboolean val = ve_config_get_bool (cfg, GDM_KEY_CONFIG_AVAILABLE);
		if (ve_bool_equal (val, GdmConfigAvailable))
			goto update_config_ok;
		GdmConfigAvailable = val;

		notify_displays_int (GDM_NOTIFY_CONFIG_AVAILABLE, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_CHOOSER_BUTTON)) {
		gboolean val = ve_config_get_bool (cfg, GDM_KEY_CHOOSER_BUTTON);
		if (ve_bool_equal (val, GdmChooserButton))
			goto update_config_ok;
		GdmChooserButton = val;

		notify_displays_int (GDM_NOTIFY_CHOOSER_BUTTON, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_RETRYDELAY)) {
		int val = ve_config_get_int (cfg, GDM_KEY_RETRYDELAY);
		if (val == GdmRetryDelay)
			goto update_config_ok;
		GdmRetryDelay = val;

		notify_displays_int (GDM_NOTIFY_RETRYDELAY, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_DISALLOWTCP)) {
		gboolean val = ve_config_get_bool (cfg, GDM_KEY_DISALLOWTCP);
		if (ve_bool_equal (val, GdmDisallowTCP))
			goto update_config_ok;
		GdmDisallowTCP = val;

		notify_displays_int (GDM_NOTIFY_DISALLOWTCP, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_GREETER)) {
		char *val = ve_config_get_string (cfg, GDM_KEY_GREETER);
		if (strcmp (ve_sure_string (val), ve_sure_string (GdmGreeter)) == 0) {
			g_free (val);
			goto update_config_ok;
		}
		g_free (GdmGreeter);
		GdmGreeter = val;

		notify_displays_string (GDM_NOTIFY_GREETER, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_REMOTEGREETER)) {
		char *val = ve_config_get_string (cfg, GDM_KEY_REMOTEGREETER);
		if (strcmp (ve_sure_string (val), ve_sure_string (GdmRemoteGreeter)) == 0) {
			g_free (val);
			goto update_config_ok;
		}
		g_free (GdmRemoteGreeter);
		GdmRemoteGreeter = val;

		notify_displays_string (GDM_NOTIFY_REMOTEGREETER, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_TIMED_LOGIN) ||
		   is_key (key, GDM_KEY_TIMED_LOGIN_ENABLE)) {
		gboolean enable = ve_config_get_bool (cfg, GDM_KEY_TIMED_LOGIN_ENABLE);
		char *val;

		/* if not enabled, we just don't care */
		if ( ! enable && ! GdmTimedLoginEnable)
			goto update_config_ok;

		val = ve_config_get_string (cfg, GDM_KEY_TIMED_LOGIN);
		if (strcmp (ve_sure_string (val),
			    ve_sure_string (GdmTimedLogin)) == 0 &&
		    ve_bool_equal (enable, GdmTimedLoginEnable)) {
			g_free (val);
			goto update_config_ok;
		}

		GdmTimedLoginEnable = enable;
		g_free (GdmTimedLogin);
		if (GdmTimedLoginEnable) {
			GdmTimedLogin = val;
		} else {
			g_free (val);
			GdmTimedLogin = NULL;
		}

		notify_displays_string (GDM_NOTIFY_TIMED_LOGIN,
					ve_sure_string (GdmTimedLogin));

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_TIMED_LOGIN_DELAY)) {
		int val = ve_config_get_int (cfg, GDM_KEY_TIMED_LOGIN_DELAY);
		if (val == GdmTimedLoginDelay)
			goto update_config_ok;
		GdmTimedLoginDelay = val;

		notify_displays_int (GDM_NOTIFY_TIMED_LOGIN_DELAY, val);

		goto update_config_ok;
	} else if (is_key (key, GDM_KEY_XDMCP)) {
		gboolean val = ve_config_get_bool (cfg, GDM_KEY_XDMCP);
		if (ve_bool_equal (val, GdmXdmcp))
			goto update_config_ok;
		GdmXdmcp = val;

		if (GdmXdmcp) {
			if (gdm_xdmcp_init ())
				gdm_xdmcp_run ();
		} else {
			gdm_xdmcp_close ();
		}

		goto update_config_ok;
	} else if (is_key (key, "xdmcp/PARAMETERS")) {
		GdmDispPerHost = ve_config_get_int (cfg, GDM_KEY_DISPERHOST);
		GdmMaxPending = ve_config_get_int (cfg, GDM_KEY_MAXPEND);
		GdmMaxManageWait = ve_config_get_int (cfg, GDM_KEY_MAXWAIT);
		GdmMaxSessions = ve_config_get_int (cfg, GDM_KEY_MAXSESS);
		GdmIndirect = ve_config_get_bool (cfg, GDM_KEY_INDIRECT);
		GdmMaxIndirect = ve_config_get_int (cfg, GDM_KEY_MAXINDIR);
		GdmMaxIndirectWait = ve_config_get_int (cfg, GDM_KEY_MAXINDWAIT);
		GdmPingInterval = ve_config_get_int (cfg, GDM_KEY_PINGINTERVAL);
	} else if (is_key (key, GDM_KEY_UDPPORT)) {
		int val = ve_config_get_int (cfg, GDM_KEY_UDPPORT);
		if (GdmPort == val)
			goto update_config_ok;
		GdmPort = val;

		if (GdmXdmcp) {
			gdm_xdmcp_close ();
			if (gdm_xdmcp_init ())
				gdm_xdmcp_run ();
		}
	}

	ve_config_destroy (cfg);
	return FALSE;

update_config_ok:

	ve_config_destroy (cfg);
	return TRUE;
}

static void
gdm_handle_user_message (GdmConnection *conn, const char *msg, gpointer data)
{
	gdm_debug ("Handling user message: '%s'", msg);

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
				     TRUE /* handled */,
				     FALSE /* chooser */,
				     NULL, 0, NULL, NULL);
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
				     /* It is kind of ugly that we don't use
					the standard resolution for this, but
					oh well, this makes other things simpler */
				     svr->handled,
				     svr->chooser,
				     NULL, 0, NULL, NULL);
	} else if (strncmp (msg, GDM_SUP_FLEXI_XNEST " ",
		            strlen (GDM_SUP_FLEXI_XNEST " ")) == 0) {
		char *dispname = NULL, *xauthfile = NULL, *cookie = NULL;
		uid_t uid;

		extract_dispname_uid_xauthfile_cookie (msg, &dispname, &uid,
						       &xauthfile, &cookie);

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
				     TRUE /* handled */,
				     FALSE /* chooser */,
				     dispname, uid, xauthfile, cookie);

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
			gdm_connection_printf (conn,
					       "%s%s,%s,",
					       sep,
					       ve_sure_string (disp->name),
					       ve_sure_string (disp->login));
			sep = ";";
			if (disp->type == TYPE_FLEXI_XNEST) {
				gdm_connection_write
					(conn,
					 ve_sure_string (disp->xnest_disp));
			} else {
				gdm_connection_printf (conn, "%d",
						       disp->vt);
			}
		}
		gdm_connection_write (conn, "\n");
	} else if (strcmp (msg, GDM_SUP_ALL_SERVERS) == 0) {
		GSList *li;
		const char *sep = " ";
		gdm_connection_write (conn, "OK");
		for (li = displays; li != NULL; li = li->next) {
			GdmDisplay *disp = li->data;
			gdm_connection_printf (conn,
					       "%s%s,%s",
					       sep,
					       ve_sure_string (disp->name),
					       ve_sure_string (disp->login));
			sep = ";";
		}
		gdm_connection_write (conn, "\n");
	} else if (strcmp (msg, GDM_SUP_GREETERPIDS) == 0) {
		GSList *li;
		const char *sep = " ";
		gdm_connection_write (conn, "OK");
		for (li = displays; li != NULL; li = li->next) {
			GdmDisplay *disp = li->data;
			if (disp->greetpid > 0) {
				gdm_connection_printf (conn, "%s%ld",
						       sep, (long)disp->greetpid);
				sep = ";";
			}
		}
		gdm_connection_write (conn, "\n");
	} else if (strncmp (msg, GDM_SUP_UPDATE_CONFIG " ",
		     strlen (GDM_SUP_UPDATE_CONFIG " ")) == 0) {
		const char *key = 
			&msg[strlen (GDM_SUP_UPDATE_CONFIG " ")];

		if ( ! update_config (key)) {
			gdm_connection_write (conn,
					      "ERROR 50 Unsupported key\n");
		} else {
			gdm_connection_write (conn, "OK\n");
		}
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
