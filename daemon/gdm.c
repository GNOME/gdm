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
#include "xdmcp.h"
#include "verify.h"
#include "display.h"

static const gchar RCSid[]="$Id$";


/* Local functions */
static void gdm_config_parse (void);
static void gdm_local_servers_start (GdmDisplay *d);
static void gdm_daemonify (void);
static void gdm_local_servers_start (GdmDisplay *d);
static void gdm_child_handler (gint sig);
static void gdm_term_handler (int sig);

/* Global vars */
GSList *displays;		/* List of displays managed */
gint sessions = 0;		/* Number of remote sessions */
sigset_t sysmask;		/* Inherited system signal mask */
gchar *argdelim = " ";		/* argv argument delimiter */
uid_t GdmUserId;		/* Userid under which gdm should run */
gid_t GdmGroupId;		/* Groupid under which gdm should run */

/* Configuration options */
gchar *GdmUser = NULL;
gchar *GdmGroup = NULL;
gchar *GdmSessDir = NULL;
gchar *GdmGreeter = NULL;
gchar *GdmChooser = NULL;
gchar *GdmLogDir = NULL;
gchar *GdmDisplayInit = NULL;
gchar *GdmPreSession = NULL;
gchar *GdmPostSession = NULL;
gchar *GdmHalt = NULL;
gchar *GdmReboot = NULL;
gchar *GdmServAuthDir = NULL;
gchar *GdmUserAuthDir = NULL;
gchar *GdmUserAuthFile = NULL;
gchar *GdmUserAuthFB = NULL;
gchar *GdmPidFile = NULL;
gchar *GdmDefaultPath = NULL;
gchar *GdmRootPath = NULL;
gint  GdmKillInitClients = 0;
gint  GdmUserMaxFile = 0;
gint  GdmXdmcp = 0;
gint  GdmDispPerHost = 0;
gint  GdmMaxPending = 0;
gint  GdmMaxManageWait = 0;
gint  GdmMaxSessions = 0;
gint  GdmPort = 0;
gint  GdmIndirect = 0;
gint  GdmMaxIndirect = 0;
gint  GdmMaxIndirectWait = 0;
gint  GdmDebug = 0;
gint  GdmVerboseAuth = 0;
gint  GdmAllowRoot = 0;
gint  GdmRelaxPerms = 0;
gint  GdmRetryDelay = 0;


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
	gdm_fail (_("gdm_config_parse: No configuration file: %s. Aborting."), GDM_CONFIG_FILE);

    /* Parse configuration options */
    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    GdmChooser = gnome_config_get_string (GDM_KEY_CHOOSER);
    GdmDefaultPath = gnome_config_get_string (GDM_KEY_PATH);
    GdmDisplayInit = gnome_config_get_string (GDM_KEY_INITDIR);
    GdmGreeter = gnome_config_get_string (GDM_KEY_GREETER);
    GdmGroup = gnome_config_get_string (GDM_KEY_GROUP);
    GdmHalt = gnome_config_get_string (GDM_KEY_HALT);
    GdmKillInitClients = gnome_config_get_int (GDM_KEY_KILLIC);
    GdmLogDir= gnome_config_get_string (GDM_KEY_LOGDIR);
    GdmPidFile = gnome_config_get_string (GDM_KEY_PIDFILE);
    GdmPostSession = gnome_config_get_string (GDM_KEY_POSTSESS);
    GdmPreSession = gnome_config_get_string (GDM_KEY_PRESESS);
    GdmReboot = gnome_config_get_string (GDM_KEY_REBOOT);
    GdmRetryDelay = gnome_config_get_int (GDM_KEY_RETRYDELAY);
    GdmRootPath = gnome_config_get_string (GDM_KEY_ROOTPATH);
    GdmServAuthDir = gnome_config_get_string (GDM_KEY_SERVAUTH);
    GdmSessDir= gnome_config_get_string (GDM_KEY_SESSDIR);
    GdmUser = gnome_config_get_string (GDM_KEY_USER);
    GdmUserAuthDir = gnome_config_get_string (GDM_KEY_UAUTHDIR);
    GdmUserAuthFile = gnome_config_get_string (GDM_KEY_UAUTHFILE);
    GdmUserAuthFB = gnome_config_get_string (GDM_KEY_UAUTHFB);

    GdmAllowRoot = gnome_config_get_int (GDM_KEY_ALLOWROOT);
    GdmRelaxPerms = gnome_config_get_int (GDM_KEY_RELAXPERM);
    GdmUserMaxFile = gnome_config_get_int (GDM_KEY_MAXFILE);
    GdmVerboseAuth = gnome_config_get_int (GDM_KEY_VERBAUTH);

    GdmXdmcp = gnome_config_get_int (GDM_KEY_XDMCP);
    GdmDispPerHost = gnome_config_get_int (GDM_KEY_DISPERHOST);
    GdmMaxPending = gnome_config_get_int (GDM_KEY_MAXPEND);
    GdmMaxManageWait = gnome_config_get_int (GDM_KEY_MAXWAIT);
    GdmMaxSessions = gnome_config_get_int (GDM_KEY_MAXSESS);
    GdmPort = gnome_config_get_int (GDM_KEY_UDPPORT);
    GdmIndirect = gnome_config_get_int (GDM_KEY_INDIRECT);
    GdmMaxIndirect = gnome_config_get_int (GDM_KEY_MAXINDIR);
    GdmMaxIndirectWait = gnome_config_get_int (GDM_KEY_MAXINDWAIT);    

    GdmDebug = gnome_config_get_int (GDM_KEY_DEBUG);

    gnome_config_pop_prefix();


    /* Prerequisites */ 
    if (GdmGreeter == NULL)
	gdm_fail (_("gdm_config_parse: No greeter specified."));

    if (GdmServAuthDir == NULL)
	gdm_fail (_("gdm_config_parse: No authdir specified."));

    if (GdmLogDir == NULL) 
	GdmLogDir = GdmServAuthDir;

    if (GdmSessDir == NULL) 
	gdm_fail (_("gdm_config_parse: No sessions directory specified."));


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

    if (! displays && ! GdmXdmcp) 
	gdm_fail (_("gdm_config_parse: Xdmcp disabled and no local servers defined. Aborting!"));


    /* Lookup user and groupid for the gdm user */
    pwent = getpwnam (GdmUser);

    if (! pwent)
	gdm_fail (_("gdm_config_parse: Can't find the gdm user (%s). Aborting!"), GdmUser);
    else 
	GdmUserId = pwent->pw_uid;

    if (GdmUserId == 0)
	gdm_fail (_("gdm_config_parse: The gdm user should not be root. Aborting!"));

    grent = getgrnam (GdmGroup);

    if (!grent)
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

	if (access (gtemp, R_OK|X_OK))
	    gdm_fail ("gdm_config_parse: Greeter not found or can't be executed by the gdm user", gtemp);
    }

    g_free (gtemp);


    /* Check that chooser can be executed */
    gtemp = g_strdup (GdmChooser);
    stemp = strchr (gtemp, ' ');

    if (GdmIndirect && stemp) {
	*stemp = '\0';

	if (access (gtemp, R_OK|X_OK))
	    gdm_fail ("gdm_config_parse: Chooser not found or it can't be executed by the gdm user", gtemp);
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


    /* Check that PAM configuration file is in place */
    gdm_verify_check ();

    seteuid (0);
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
	gdm_slave_start (d);
    }
}


/**
 * gdm_child_handler:
 * @sig: Signal value
 *
 * ACME Funeral Services
 */

static void 
gdm_child_handler (gint sig)
{
    pid_t pid;
    gint exitstatus = 0, status = 0;
    GSList *list = displays;
    GdmDisplay *d;
    gchar **argv;

    /* Get status from all dead children */
    while ((pid = waitpid (-1, &exitstatus, WNOHANG)) > 0) {

	if (WIFEXITED (exitstatus))
	    status = WEXITSTATUS (exitstatus);
	
	gdm_debug ("gdm_child_handler: child %d returned %d", pid, status);

	if (pid < 1)
	    break;

	while (list && list->data) {

	    d = list->data;
	    gdm_debug ("gdm_child_handler: %s", d->name);

	    /* Slave died */
	    if (pid == d->slavepid) {
		d->slavepid = 0;
	
		switch (status) {

		case DISPLAY_REMANAGE:

		    if (d->type == TYPE_LOCAL) {
			d->dispstat = DISPLAY_DEAD;
			gdm_slave_start (d);
		    }
		    
		    if (d->type == TYPE_XDMCP)
			gdm_display_unmanage (d);
		    
		    break;
		    		    
		case DISPLAY_ABORT:
		    gdm_info (_("gdm_child_action: Aborting display %s"), d->name);
		    gdm_display_unmanage (d);
		    break;
		    
		case DISPLAY_REBOOT:
		    gdm_info (_("gdm_child_action: Master rebooting..."));
		    g_slist_foreach (displays, (GFunc) gdm_display_unmanage, NULL);
		    closelog();
		    unlink (GdmPidFile);
		    argv = g_strsplit (GdmReboot, argdelim, MAX_ARGS);
		    execv (argv[0], argv);
		    gdm_error (_("gdm_child_action: Reboot failed: %s"), strerror (errno));
		    break;
		    
		case DISPLAY_HALT:
		    gdm_info (_("gdm_child_action: Master halting..."));
		    g_slist_foreach (displays, (GFunc) gdm_display_unmanage, NULL);
		    closelog();
		    unlink (GdmPidFile);
		    argv = g_strsplit (GdmHalt, argdelim, MAX_ARGS);
		    execv (argv[0], argv);
		    gdm_error (_("gdm_child_action: Halt failed: %s"), strerror (errno));
		    break;
		    
		case DISPLAY_RESERVER:
		default:
		    gdm_debug ("gdm_child_action: Slave process returned %d", status);

		    if (d->type == TYPE_LOCAL && d->dispstat != DISPLAY_ABORT) {
			d->dispstat = DISPLAY_DEAD;
			gdm_server_start (d);
		    }
		    
		    if (d->type == TYPE_XDMCP)
			gdm_display_unmanage (d);
		    
		    break;
		}
	    }

	    list = list->next;
	}
    }
}


/**
 * gdm_term_handler:
 * @sig: Signal value
 *
 * Shutdown all displays and terminate the gdm process
 */

static void
gdm_term_handler (int sig)
{
    sigset_t mask;

    gdm_debug ("gdm_term_handler: Got TERM/INT. Going down!");

    /* Block SIGCHLD */
    sigemptyset (&mask);
    sigaddset (&mask, SIGCHLD);
    sigprocmask (SIG_BLOCK, &mask, NULL); 

    /* Unmanage all displays */
    g_slist_foreach (displays, (GFunc) gdm_display_unmanage, NULL);

    /* Cleanup */
    closelog();
    unlink (GdmPidFile);

    exit (EXIT_SUCCESS);
}


/*
 * main: The main daemon control
 */

int 
main (int argc, char *argv[])
{
    sigset_t mask;
    struct sigaction term, child;
    FILE *pf;
    GMainLoop *main_loop;    
 
    /* XDM compliant error message */
    if (getuid())
	gdm_fail (_("Only root wants to run x^hgdm\n"));

    /* Initialize runtime environment */
    umask (022);
    gnome_do_not_create_directories = TRUE;
    gnomelib_init ("gdm", VERSION);
    main_loop = g_main_new (FALSE);
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    /* Parse configuration file */
    gdm_config_parse();

    /* Check if another gdm process is already running */
    if (! access (GdmPidFile, R_OK)) {

        /* Check if the existing process is still alive. */
        gint pidv;

        pf = fopen (GdmPidFile, "r");

        if (pf && fscanf (pf, "%d", &pidv) == 1 && kill (pidv, 0) != -1)
		gdm_fail (_("gdm already running. Aborting!"));
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
    term.sa_handler = gdm_term_handler;
    term.sa_flags = SA_RESTART;
    sigemptyset (&term.sa_mask);

    if (sigaction (SIGTERM, &term, NULL) < 0) 
	gdm_fail (_("gdm_main: Error setting up TERM signal handler"));

    if (sigaction (SIGINT, &term, NULL) < 0) 
	gdm_fail (_("gdm_main: Error setting up INT signal handler"));

    child.sa_handler = gdm_child_handler;
    child.sa_flags = SA_RESTART|SA_NOCLDSTOP;
    sigemptyset (&child.sa_mask);
    sigaddset (&child.sa_mask, SIGCHLD);

    if (sigaction (SIGCHLD, &child, NULL) < 0) 
	gdm_fail (_("gdm_main: Error setting up CHLD signal handler"));

    sigemptyset (&mask);
    sigaddset (&mask, SIGINT);
    sigaddset (&mask, SIGTERM);
    sigaddset (&mask, SIGCHLD);
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

    g_main_run (main_loop);

    return EXIT_SUCCESS;	/* Not reached */
}


/* EOF */
