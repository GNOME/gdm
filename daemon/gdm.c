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
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>

#include "gdm.h"
#include "misc.h"
#include "slave.h"
#include "server.h"
#include "xdmcp.h"
#include "verify.h"
#include "display.h"
#include "errorgui.h"

static const gchar RCSid[]="$Id$";


/* Local functions */
static void gdm_config_parse (void);
static void gdm_local_servers_start (GdmDisplay *d);
static void gdm_daemonify (void);

/* Global vars */
GSList *displays;		/* List of displays managed */
gint sessions = 0;		/* Number of remote sessions */
sigset_t sysmask;		/* Inherited system signal mask */
gchar *argdelim = " ";		/* argv argument delimiter */
uid_t GdmUserId;		/* Userid under which gdm should run */
gid_t GdmGroupId;		/* Groupid under which gdm should run */

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
gchar *GdmXKeepsCrashingConfigurators = NULL;
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
gboolean  GdmDebug = FALSE;
gboolean  GdmVerboseAuth = FALSE;
gboolean  GdmAllowRoot = FALSE;
gboolean  GdmAllowRemoteRoot = FALSE;
gboolean  GdmAllowRemoteAutoLogin = FALSE;
gint  GdmRelaxPerms = 0;
gint  GdmRetryDelay = 0;
gchar *GdmTimedLogin = NULL;
gboolean GdmTimedLoginEnable = FALSE;
gint GdmTimedLoginDelay = 0;

/* set in the main function */
char **stored_argv = NULL;
int stored_argc = 0;
char *stored_path = NULL;


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
    gchar *gtemp, *stemp;
    
    displays = NULL;

    if (stat (GDM_CONFIG_FILE, &statbuf) == -1)
	gdm_error (_("gdm_config_parse: No configuration file: %s. Using defaults."), GDM_CONFIG_FILE);

    /* Parse configuration options */
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GdmChooser = gnome_config_get_string (GDM_KEY_CHOOSER);
    GdmDefaultPath = gnome_config_get_string (GDM_KEY_PATH);
    GdmDisplayInit = gnome_config_get_string (GDM_KEY_INITDIR);
    GdmAutomaticLoginEnable = gnome_config_get_bool (GDM_KEY_AUTOMATICLOGIN_ENABLE);
    GdmAutomaticLogin = gnome_config_get_string (GDM_KEY_AUTOMATICLOGIN);
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
    GdmXKeepsCrashingConfigurators = gnome_config_get_string (GDM_KEY_XKEEPSCRASHING_CONFIGURATORS);
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
    GdmVerboseAuth = gnome_config_get_bool (GDM_KEY_VERBAUTH);

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
	gdm_string_empty (GdmAutomaticLogin)) {
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
	gdm_string_empty (GdmTimedLogin)) {
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

    /* Prerequisites */ 
    if (gdm_string_empty (GdmGreeter)) {
	    gdm_error (_("gdm_config_parse: No greeter specified."));
    }

    if (gdm_string_empty (GdmServAuthDir))
	gdm_fail (_("gdm_config_parse: No authdir specified."));

    if (gdm_string_empty (GdmLogDir))
	GdmLogDir = GdmServAuthDir;

    if (gdm_string_empty (GdmSessDir))
	gdm_error (_("gdm_config_parse: No sessions directory specified."));


    /* Find local X server definitions */
    iter = gnome_config_init_iterator ("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS);
    iter = gnome_config_iterator_next (iter, &k, &v);
    
    while (iter) {

	if (isdigit (*k))
	    displays = g_slist_append (displays, gdm_server_alloc (atoi (k), v));
	else
	    gdm_info (_("gdm_config_parse: Invalid server line in config file. Ignoring!"));

	iter = gnome_config_iterator_next (iter, &k, &v);
    }

    if (! displays && ! GdmXdmcp) {
	    /* yay, we can add a backup emergency server */
	    if (access ("/usr/bin/X11/X", X_OK) == 0) {
		    gdm_error (_("gdm_config_parse: Xdmcp disabled and no local servers defined. Adding /usr/bin/X11/X on :0 to allow configuration!"));

		    gdm_emergency_server = TRUE;
		    displays = g_slist_append
			    (displays, gdm_server_alloc (0, "/usr/bin/X11/X"));
		    /* ALWAYS run the greeter and don't log anyone in,
		     * this is just an emergency session */
		    g_free (GdmAutomaticLogin);
		    GdmAutomaticLogin = NULL;
		    g_free (GdmTimedLogin);
		    GdmTimedLogin = NULL;
	    } else {
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

    if (pwent == NULL)
	    gdm_fail (_("gdm_config_parse: Can't find the gdm user (%s). Aborting!"), GdmUser);
    else 
	    GdmUserId = pwent->pw_uid;

    if (GdmUserId == 0)
	gdm_fail (_("gdm_config_parse: The gdm user should not be root. Aborting!"));

    grent = getgrnam (GdmGroup);

    if (grent == NULL) {
	    gdm_error (_("gdm_config_parse: Can't find the gdm group (%s). Trying 'nobody'!"), GdmUser);
	    g_free (GdmGroup);
	    GdmGroup = g_strdup ("nobody");
	    pwent = getpwnam (GdmUser);
    }

    if (grent == NULL)
	gdm_fail (_("gdm_config_parse: Can't find the gdm group (%s). Aborting!"), GdmGroup);
    else 
	GdmGroupId = grent->gr_gid;   

    if (GdmGroupId == 0)
	gdm_fail (_("gdm_config_parse: The gdm group should not be root. Aborting!"));

    setegid (GdmGroupId);	/* gid remains `gdm' */
    seteuid (GdmUserId);


    /* Check that the greeter can be executed */
    gtemp = g_strdup (GdmGreeter);
    stemp = strchr (gtemp, ' ');

    if (stemp) {
	*stemp = '\0';

	if (access (gtemp, X_OK))
	    gdm_error ("gdm_config_parse: Greeter not found or can't be executed by the gdm user", gtemp);
    }

    g_free (gtemp);


    /* Check that chooser can be executed */
    gtemp = g_strdup (GdmChooser);
    stemp = strchr (gtemp, ' ');

    if (GdmIndirect && stemp) {
	*stemp = '\0';

	if (access (gtemp, X_OK))
	    gdm_error ("gdm_config_parse: Chooser not found or it can't be executed by the gdm user", gtemp);
    }
    
    g_free (gtemp);


    /* Enter paranoia mode */
    if (stat (GdmServAuthDir, &statbuf) == -1) 
	gdm_fail (_("gdm_config_parse: Authdir %s does not exist. Aborting."), GdmServAuthDir);

    if (! S_ISDIR (statbuf.st_mode))
	gdm_fail (_("gdm_config_parse: Authdir %s is not a directory. Aborting."), GdmServAuthDir);

    if (statbuf.st_uid != GdmUserId || statbuf.st_gid != GdmGroupId) 
	gdm_fail (_("gdm_config_parse: Authdir %s is not owned by user %s, group %s. Aborting."), 
		  GdmServAuthDir, GdmUser, GdmGroup);

    if (statbuf.st_mode != (S_IFDIR|S_IRWXU|S_IRGRP|S_IXGRP)) 
	gdm_fail (_("gdm_config_parse: Authdir %s has wrong permissions. Should be 750. Aborting."), 
		  GdmServAuthDir, statbuf.st_mode);

    seteuid (0);
    setegid (0);

    /* Check that user authentication is properly configured */
    gdm_verify_check ();
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

    if ((pid = fork ())) {

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

    open ("/dev/null", O_RDONLY);
    dup2 (0, 1);
    dup2 (0, 2);
}


/**
 * gdm_local_servers_start:
 * @d: Pointer to a GdmDisplay struct
 *
 * Start all local (i.e. non-XDMCP) X servers
 */

static void 
gdm_local_servers_start (GdmDisplay *d)
{
    if (d && d->type == TYPE_LOCAL) {
	gdm_debug ("gdm_local_servers_start: Starting %s", d->name);

	/* only the first local display has timed login going on */
	if (gdm_first_login)
		d->timed_login_ok = TRUE;

	if ( ! gdm_display_manage (d)) {
		gdm_display_unmanage (d);
		/* only the first local display gets autologged in */
		gdm_first_login = FALSE;
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

	list = g_slist_copy (displays);
	g_slist_foreach (list, (GFunc) gdm_display_unmanage, NULL);
	g_slist_free (list);

	gdm_xdmcp_close ();

	closelog();
	unlink (GdmPidFile);
}

static gboolean
deal_with_x_crashes (GdmDisplay *d)
{
    gboolean just_abort = FALSE;
    char *msg;

    if ( ! d->failsafe_xserver &&
	 ! gdm_string_empty (GdmFailsafeXServer)) {
	    char *bin = g_strdup (GdmFailsafeXServer);
	    char *p = strchr (bin, argdelim[0]);
	    if (p != NULL)
		    *p = '\0';
	    /* Yay we have a failsafe */
	    if (access (bin, X_OK) == 0) {
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
    if ( ! gdm_string_empty (GdmXKeepsCrashing) &&
	 ! gdm_string_empty (GdmXKeepsCrashingConfigurators) &&
	access (GdmXKeepsCrashing, X_OK|R_OK) == 0) {
	    char tempname[256];
	    int tempfd;
	    pid_t pid;
	    char **configurators;
	    int i;

	    configurators = g_strsplit (GdmXKeepsCrashingConfigurators,
					argdelim, MAX_ARGS);	
	    for (i = 0; configurators[i] != NULL; i++) {
		    if (access (configurators[i], X_OK) == 0)
			    break;
	    }

	    if (configurators[i] != NULL) {
		    strcpy (tempname, "/tmp/gdm-X-failed-XXXXXX");
		    tempfd = mkstemp (tempname);
		    close (tempfd);

		    gdm_info (_("deal_with_x_crashes: Running the "
				"XKeepsCrashing script"));

		    pid = fork ();
	    } else {
		    tempfd = -1;
		    /* no forking, we're screwing this */
		    pid = -1;
	    }
	    if (pid == 0) {
		    char *argv[9];

		    argv[0] = GdmXKeepsCrashing;
		    argv[1] = configurators[i];
		    argv[2] = tempname;
		    argv[3] = _("I cannot start the X server (your graphical "
				"interface).  It is likely that it is not set "
				"up correctly.  You will need to log in on a "
				"console and rerun the X configuration "
				"program.  Then restart GDM.");
		    argv[4] = _("I cannot start the X server (your graphical "
				"interface).  It is likely that it is not set "
				"up correctly.  Would you like me to try to "
				"run the X configuration program?  Note that "
				"you will need the root password for this.");
		    argv[5] = _("Please type in the root (privilaged user) "
				"password.");
		    argv[6] = _("I will now try to restart the X server "
				"again.");
		    argv[7] = _("I will disable this X server for now.  "
				"Restart GDM when it is configured correctly.");
		    argv[8] = NULL;

		    execv (argv[0], argv);
	
		    /* yaikes! */
		    _exit (32);
	    } else if (pid > 0) {
		    int status;
		    waitpid (pid, &status, 0);
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

	    if (tempfd >= 0)
		    unlink (tempname);

	    /* if we failed to fork, or something else has happened,
	     * we fall through to the other options below */
    }


    /* if we have "open" we can talk to the user, not as user
     * friendly as the above script, but getting there */
    if ( ! just_abort &&
	access ("/usr/bin/open", X_OK) == 0) {
	    char *dialog; /* do we have dialog?*/
	    dialog = gnome_is_program_in_path ("dialog");
	    if (dialog == NULL)
		    dialog = gnome_is_program_in_path ("gdialog");
	    if (dialog != NULL) {
		    char *command = 
			    g_strdup_printf
			    ("/usr/bin/open -s -w -- /bin/sh -c 'clear ; "
			     "%s --msgbox \"%s\" 10 70 ; clear'",
			     dialog,
			     _("I cannot start the X server (your graphical "
			       "interface).  It is likely that it is not set "
			       "up correctly.  You will need to log in on a "
			       "console and rerun the X configuration "
			       "program.  Then restart GDM."));
		    /* Shit if we knew what the program was to tell the user,
		     * the above script would have been defined and we'd run
		     * it for them */
		    system (command);
	    } else {
		    char *command = 
			    g_strdup_printf
			    ("/usr/bin/open -s -w -- /bin/sh -c 'clear ; "
			     "echo \"%s\" 10 70' ; clear",
			     _("I cannot start the X server (your graphical "
			       "interface).  It is likely that it is not set "
			       "up correctly.  You will need to log in on a "
			       "console and rerun the X configuration "
			       "program.  Then restart GDM."));
		    /* Shit if we knew what the program was to tell the user,
		     * the above script would have been defined and we'd run
		     * it for them */
		    system (command);
	    }
    } /* else {
       * At this point .... screw the user, we don't know how to
       * talk to him.  He's on some 'l33t system anyway, so syslog
       * reading will do him good 
       * } */

    msg = g_strdup_printf (_("Failed to start X server several times in a short time period; disabling display %s"), d->name);
    gdm_error (msg);
    g_free (msg);

    return FALSE;
}

static gboolean
bin_executable (const char *command)
{
	char **argv;

	if (gdm_string_empty (command))
		return FALSE;

	argv = g_strsplit (command, argdelim, MAX_ARGS);	
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

static void 
gdm_cleanup_children (void)
{
    pid_t pid;
    gint exitstatus = 0, status;
    GdmDisplay *d = NULL;
    gchar **argv;

    /* Pid and exit status of slave that died */
    pid = waitpid (-1, &exitstatus, WNOHANG);

    if (WIFEXITED (exitstatus))
	    status = WEXITSTATUS (exitstatus);
    else
	    status = DISPLAY_SUCCESS;
	
    gdm_debug ("gdm_cleanup_children: child %d returned %d", pid, status);

    if (pid < 1)
	return;

    /* Find out who this slave belongs to */
    d = gdm_display_lookup (pid);

    if (!d)
	return;

    /* Declare the display dead */
    d->slavepid = 0;
    d->dispstat = DISPLAY_DEAD;

    if ( ! GdmSystemMenu &&
	(status == DISPLAY_REBOOT ||
	 status == DISPLAY_HALT)) {
	    gdm_info (_("gdm_child_action: Reboot or Halt request when there is no system menu from display %s"), d->name);
	    status = DISPLAY_REMANAGE;
    }

    if (d->type != TYPE_LOCAL &&
	(status == DISPLAY_RESTARTGDM ||
	 status == DISPLAY_REBOOT ||
	 status == DISPLAY_HALT)) {
	    gdm_info (_("gdm_child_action: Restart, Reboot or Halt request from a non-local display %s"), d->name);
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

	gdm_display_unmanage (d);
	break;
	
    case DISPLAY_REBOOT:	/* Reboot machine */
	gdm_info (_("gdm_child_action: Master rebooting..."));

	final_cleanup ();

	argv = g_strsplit (GdmReboot, argdelim, MAX_ARGS);	
	execv (argv[0], argv);

	gdm_error (_("gdm_child_action: Reboot failed: %s"), strerror (errno));
	break;
	
    case DISPLAY_HALT:		/* Halt machine */
	gdm_info (_("gdm_child_action: Master halting..."));

	final_cleanup ();

	argv = g_strsplit (GdmHalt, argdelim, MAX_ARGS);	
	execv (argv[0], argv);

	gdm_error (_("gdm_child_action: Halt failed: %s"), strerror (errno));
	break;

    case DISPLAY_SUSPEND:	/* Suspend machine */
	gdm_info (_("gdm_child_action: Master suspending..."));

	final_cleanup ();

	argv = g_strsplit (GdmReboot, argdelim, MAX_ARGS);	
	execv (argv[0], argv);

	gdm_error (_("gdm_child_action: Suspend failed: %s"), strerror (errno));
	break;
	

    case DISPLAY_RESTARTGDM:
	final_cleanup ();
	if (stored_path != NULL)
		putenv (stored_path);
	execvp (stored_argv[0], stored_argv);
	gdm_error (_("Failed to restart self"));
	_exit (1);
	break;

    case DISPLAY_XFAILED:       /* X sucks */
	/* in remote case just drop to _REMANAGE */
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
	
	/* This is a local server so we start a new slave */
	if (d->type == TYPE_LOCAL) {
		if ( ! gdm_display_manage (d))
			gdm_display_unmanage (d);
	/* Remote displays will send a request to be managed */
	} else if (d->type == TYPE_XDMCP) {
		gdm_display_unmanage (d);
	}
	
	break;
    }

    gdm_quit ();
}

static void
term_cleanup (void)
{
	gdm_debug ("term_cleanup: Got TERM/INT. Going down!");

	final_cleanup ();
}



static gboolean
mainloop_sig_callback (gint8 sig, gpointer data)
{
  gdm_debug ("mainloop_sig_callback: Got signal %d", (int)sig);
  switch (sig)
    {
    case SIGCHLD:
      gdm_cleanup_children ();
      break;

    case SIGINT:
    case SIGTERM:
      term_cleanup ();
      exit (EXIT_SUCCESS);
      break;

    case SIGHUP:
      term_cleanup ();
      if (stored_path != NULL)
	      putenv (stored_path);
      execvp (stored_argv[0], stored_argv);
      gdm_error (_("Failed to restart self"));
      _exit (1);
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

    /* XDM compliant error message */
    if (getuid() != 0)
	gdm_fail (_("Only root wants to run gdm\n"));


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
	    kill (pidv, 0) != -1)
		gdm_fail (_("gdm already running. Aborting!"));

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
    
    term.sa_handler = signal_notify;
    term.sa_flags = SA_RESTART;
    sigemptyset (&term.sa_mask);

    if (sigaction (SIGTERM, &term, NULL) < 0) 
	gdm_fail (_("gdm_main: Error setting up TERM signal handler"));

    if (sigaction (SIGINT, &term, NULL) < 0) 
	gdm_fail (_("gdm_main: Error setting up INT signal handler"));

    if (sigaction (SIGHUP, &term, NULL) < 0) 
	gdm_fail (_("gdm_main: Error setting up HUP signal handler"));

    child.sa_handler = signal_notify;
    child.sa_flags = SA_RESTART|SA_NOCLDSTOP;
    sigemptyset (&child.sa_mask);
    sigaddset (&child.sa_mask, SIGCHLD);

    if (sigaction (SIGCHLD, &child, NULL) < 0) 
	gdm_fail (_("gdm_main: Error setting up CHLD signal handler"));

    sigemptyset (&mask);
    sigaddset (&mask, SIGINT);
    sigaddset (&mask, SIGTERM);
    sigaddset (&mask, SIGCHLD);
    sigaddset (&mask, SIGHUP);
    sigprocmask (SIG_UNBLOCK, &mask, &sysmask); /* Save system sigmask */

    gdm_debug ("gdm_main: Here we go...");

    /* Init XDMCP if applicable */
    if (GdmXdmcp)
	gdm_xdmcp_init();

    /* Start local X servers */
    g_slist_foreach (displays, (GFunc) gdm_local_servers_start, NULL);

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

/* EOF */
