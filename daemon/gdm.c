/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999 Martin Kasper Petersen <mkp@SunSITE.auc.dk>
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

#include <gnome.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <strings.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <config.h>
#include <X11/Xauth.h>

#include "gdm.h"

static const gchar RCSid[]="$Id$";

extern void gdm_slave_start (GdmDisplay *);
extern gchar **gdm_arg_munch (const gchar *p);
extern void gdm_fail (const gchar *, ...);
extern void gdm_info (const gchar *, ...);
extern void gdm_error (const gchar *, ...);
extern GdmDisplay *gdm_server_alloc (gint id, gchar *command);
extern void gdm_server_start (GdmDisplay *d);
extern void gdm_server_stop (GdmDisplay *d);
extern void gdm_server_restart (GdmDisplay *d);
extern void gdm_debug (const gchar *format, ...);
extern int  gdm_xdmcp_init (void);
extern void gdm_xdmcp_run (void);
extern void gdm_xdmcp_close (void);
extern void gdm_verify_check (void);

gint gdm_display_manage (GdmDisplay *d);
void gdm_display_dispose (GdmDisplay *d);
static void gdm_local_servers_start (GdmDisplay *d);
static void gdm_display_unmanage (GdmDisplay *d);

GSList *displays;
gint sessions=0;
static gchar *GdmUser;
static gchar *GdmGroup;
sigset_t sysmask;

uid_t GdmUserId;
gid_t GdmGroupId;
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
gint  GdmXdmcp;
gint  GdmMaxPending;
gint  GdmMaxManageWait;
gint  GdmMaxSessions;
gint  GdmPort;
gint  GdmIndirect;
gint  GdmMaxIndirect;
gint  GdmMaxIndirectWait;
gint  GdmDebug;
gint  GdmVerboseAuth;
gint  GdmAllowRoot;
gint  GdmRelaxPerms;
gint  GdmRetryDelay;

typedef struct _childstat ChildStat;
struct _childstat { pid_t pid; gint status; };


void
gdm_display_dispose (GdmDisplay *d)
{
    GSList *tmpauth;

    if (!d)
	return;

    if (d->type == DISPLAY_XDMCP) {
	displays = g_slist_remove (displays, d);
	sessions--;
    }

    if (d->name) {
	gdm_debug ("gdm_display_dispose: Disposing %s", d->name);
	g_free (d->name);
    }

    if (d->hostname)
	g_free (d->hostname);

    if (d->authfile)
	g_free (d->authfile);

    if (d->auths) {
	tmpauth = d->auths;

	while (tmpauth && tmpauth->data) {
	    XauDisposeAuth ((Xauth *) tmpauth->data);
	    tmpauth = tmpauth->next;
	}

	g_slist_free (d->auths);
    }

    if (d->userauth)
	g_free (d->userauth);

    if (d->command)
	g_free (d->command);

    if (d->cookie)
	g_free (d->cookie);

    if (d->bcookie)
	g_free (d->bcookie);

    g_free (d);
}


static void 
gdm_config_parse (void)
{
    gchar *k, *v;
    void *iter;
    struct passwd *pwent;
    struct group *grent;
    struct stat statbuf;
    
    displays = NULL;

    if (stat (GDM_CONFIG_FILE, &statbuf) == -1)
	gdm_fail (_("gdm_config_parse: No configuration file: %s. Aborting."), GDM_CONFIG_FILE);

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
    GdmMaxPending = gnome_config_get_int (GDM_KEY_MAXPEND);
    GdmMaxManageWait = gnome_config_get_int (GDM_KEY_MAXWAIT);
    GdmMaxSessions = gnome_config_get_int (GDM_KEY_MAXSESS);
    GdmPort = gnome_config_get_int (GDM_KEY_UDPPORT);
    GdmIndirect = gnome_config_get_int (GDM_KEY_INDIRECT);
    GdmMaxIndirect = gnome_config_get_int (GDM_KEY_MAXINDIR);
    GdmMaxIndirectWait = gnome_config_get_int (GDM_KEY_MAXINDWAIT);    

    GdmDebug = gnome_config_get_int (GDM_KEY_DEBUG);

    if (GdmGreeter==NULL && stat ("/usr/local/bin/gdmlogin", &statbuf)==0)
    	GdmGreeter = "/usr/local/bin/gdmlogin";

    if (GdmGreeter==NULL && stat ("/usr/bin/gdmlogin", &statbuf)==0)
    	GdmGreeter = "/usr/bin/gdmlogin";

    if (GdmGreeter==NULL && stat ("/opt/gnome/bin/gdmlogin", &statbuf)==0)
    	GdmGreeter = "/opt/gnome/bin/gdmlogin";
    	
    if (GdmGreeter==NULL)
	gdm_fail (_("gdm_config_parse: No greeter specified and default not found."));

    if (GdmServAuthDir==NULL && stat ("/var/gdm", &statbuf)==0)
    	GdmServAuthDir = "/var/gdm";

    if (GdmServAuthDir==NULL && stat ("/var/lib/gdm", &statbuf)==0)
    	GdmServAuthDir = "/var/lib/gdm";

    if (GdmServAuthDir==NULL && stat ("/opt/gnome/var/gdm", &statbuf)==0)
    	GdmServAuthDir = "/opt/gnome/var/gdm";
    	
    if (GdmServAuthDir==NULL)
	gdm_fail (_("gdm_config_parse: No authdir specified and default not found."));

    if (GdmLogDir==NULL) 
	GdmLogDir = GdmServAuthDir;

    if (GdmSessDir==NULL && stat ("/usr/local/etc/gdm/Sessions", &statbuf)==0)
    	GdmSessDir = "/usr/local/etc/gdm/Sessions";

    if (GdmSessDir==NULL && stat ("/usr/etc/gdm/Sessions", &statbuf)==0)
    	GdmSessDir = "/usr/etc/gdm/Sessions";    

    if (GdmSessDir==NULL && stat ("/etc/gdm/Sessions", &statbuf)==0)
    	GdmSessDir = "/etc/gdm/Sessions";    

    if (GdmSessDir==NULL && stat ("/etc/X11/gdm/Sessions", &statbuf)==0)
    	GdmSessDir = "/etc/X11/gdm/Sessions";    

    if (GdmSessDir==NULL) 
	gdm_fail (_("gdm_config_parse: No sessions directory specified and default not found."));

    gnome_config_pop_prefix();

    iter = gnome_config_init_iterator ("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS);
    iter = gnome_config_iterator_next (iter, &k, &v);
    
    while (iter) {

	if (isdigit(*k))
	    displays = g_slist_append (displays, gdm_server_alloc (atoi(k), v));
	else
	    gdm_info (_("gdm_config_parse: Invalid server line in config file. Ignoring!"));

	iter = gnome_config_iterator_next (iter, &k, &v);
    }

    if (!displays && !GdmXdmcp) 
	gdm_fail (_("gdm_config_parse: Xdmcp disabled and no local servers defined. Aborting!"));

    pwent = getpwnam (GdmUser);

    if (!pwent)
	gdm_fail (_("gdm_config_parse: Can't find the gdm user (%s). Aborting!"), GdmUser);
    else 
	GdmUserId = pwent->pw_uid;

    if (GdmUserId==0)
	gdm_fail (_("gdm_config_parse: The gdm user should not be root. Aborting!"));

    grent = getgrnam (GdmGroup);

    if (!grent)
	gdm_fail (_("gdm_config_parse: Can't find the gdm group (%s). Aborting!"), GdmGroup);
    else 
	GdmGroupId = grent->gr_gid;   

    if (GdmGroupId==0)
	gdm_fail (_("gdm_config_parse: The gdm group should not be root. Aborting!"));

    setegid (GdmGroupId);	/* gid remains `gdm' */
    seteuid (GdmUserId);

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


static void 
gdm_local_servers_start (GdmDisplay *d)
{
    if (d && d->type == DISPLAY_LOCAL) {
	gdm_debug ("gdm_local_servers_start: Starting %s", d->name);
	gdm_server_start (d);
    }
}


gint 
gdm_display_manage (GdmDisplay *d)
{
    sigset_t mask, omask;

    if (!d) 
	return (FALSE);

    gdm_debug ("gdm_display_manage: Managing %s", d->name);

    /* If we have an old slave process hanging around, kill it */
    if (d->slavepid) {
	sigemptyset (&mask);
	sigaddset (&mask, SIGCHLD);
	sigprocmask (SIG_BLOCK, &mask, &omask); 

	kill (d->slavepid, SIGINT);
	waitpid (d->slavepid, 0, 0);

	sigprocmask (SIG_SETMASK, &omask, NULL);
    }

    switch (d->slavepid=fork()) {

    case 0:

	setpgid (0, 0);

	/* Close XDMCP fd in slave process */
	if (GdmXdmcp)
	    gdm_xdmcp_close();

	if (d->type == DISPLAY_LOCAL && d->servstat == SERVER_RUNNING)
	    gdm_slave_start (d);

	if (d->type == DISPLAY_XDMCP && d->dispstat == XDMCP_MANAGED)
	    gdm_slave_start (d);

	break;

    case -1:
	gdm_error (_("gdm_display_manage: Failed forking gdm slave process for %d"), d->name);
	return (FALSE);

    default:
	gdm_debug ("gdm_display_manage: Forked slave: %d", d->slavepid);
	break;
    }

    return (TRUE);
}


static void 
gdm_child_handler (gint sig)
{
    pid_t pid;
    gint exitstatus = 0, status = 0;
    GSList *list = displays;
    GdmDisplay *d;
    gchar **argv;

    /* Get status from all dead children */
    while ((pid=waitpid (-1, &exitstatus, WNOHANG)) > 0) {

	if (WIFEXITED (exitstatus))
	    status=WEXITSTATUS (exitstatus);
	
	gdm_debug ("gdm_child_handler: child %d returned %d", pid, status);

	if (pid < 1)
	    return;

	while (list && list->data) {

	    d = list->data;
	    gdm_debug ("gdm_child_handler: %s", d->name);

	    /* X server died */
	    if (pid == d->servpid) {
		d->servpid = 0;

		switch (status) {

		case SERVER_SUCCESS:
		case SERVER_FAILURE:
		    gdm_server_start (d);
		    break;

		case SERVER_NOTFOUND:
		case SERVER_ABORT:
		    gdm_display_unmanage (d);
		    break;

		default:
		    gdm_debug ("gdm_child_handler: Server process returned unknown status %d", status);
		    gdm_display_unmanage (d);
		    break;
		}
	    }
    
	    /* Slave died */
	    if (pid==d->slavepid) {
		d->slavepid = 0;
	
		switch (status) {

		case DISPLAY_REMANAGE:

		    if (d->type == DISPLAY_LOCAL && d->dispstat != DISPLAY_ABORT) {
			d->dispstat = DISPLAY_DEAD;
			gdm_server_restart (d);
		    }
		    
		    if (d->type == DISPLAY_XDMCP)
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
		    argv = gdm_arg_munch (GdmReboot);
		    execv (argv[0], argv);
		    gdm_error (_("gdm_child_action: Reboot failed: %s"), strerror(errno));
		    break;
		    
		case DISPLAY_HALT:
		    gdm_info (_("gdm_child_action: Master halting..."));
		    g_slist_foreach (displays, (GFunc) gdm_display_unmanage, NULL);
		    closelog();
		    unlink (GdmPidFile);
		    argv = gdm_arg_munch (GdmHalt);
		    execv (argv[0], argv);
		    gdm_error (_("gdm_child_action: Halt failed: %s"), strerror(errno));
		    break;
		    
		case DISPLAY_RESERVER:
		default:
		    gdm_debug ("gdm_child_action: Slave process returned %d", status);

		    if (d->type == DISPLAY_LOCAL && d->dispstat != DISPLAY_ABORT) {
			d->dispstat = DISPLAY_DEAD;
			gdm_server_start (d);
		    }
		    
		    if (d->type == DISPLAY_XDMCP)
			gdm_display_unmanage (d);
		    
		    break;
		}
	    }

	    list = list->next;
	}
    }
}


static void
gdm_display_unmanage (GdmDisplay *d)
{
    if (!d)
	return;

    gdm_debug ("gdm_display_unmanage: Stopping %s", d->name);

    if (d->type == DISPLAY_LOCAL) {

	/* Kill slave and all its children */
	if (d->slavepid) {
	    kill (-(d->slavepid), SIGTERM);
	    waitpid (d->slavepid, 0, 0);
	    d->slavepid = 0;
	}

	if (d->servpid) {
	    gdm_server_stop (d);
	}

	d->dispstat = DISPLAY_DEAD;
    }
    else { /* DISPLAY_XDMCP */
	if (d->slavepid) {
	    kill (d->slavepid, SIGTERM);
	    waitpid (d->slavepid, 0, 0);
	    d->slavepid = 0;
	}

	gdm_display_dispose (d);
    }
}


static void
gdm_term_handler (int sig)
{
    sigset_t mask;

    gdm_debug ("gdm_term_handler: Got TERM/INT. Going down!");

    sigemptyset (&mask);
    sigaddset (&mask, SIGCHLD);
    sigprocmask (SIG_BLOCK, &mask, NULL); 

    g_slist_foreach (displays, (GFunc) gdm_display_unmanage, NULL);

    closelog();
    unlink (GdmPidFile);

    exit (EXIT_SUCCESS);
}


static void
gdm_daemonify (void)
{
    FILE *pf;
    pid_t pid;

    if ((pid=fork())) {

	if ((pf = fopen (GdmPidFile, "w"))) {
	    fprintf (pf, "%d\n", pid);
	    fclose (pf);
	}

        exit (EXIT_SUCCESS);
    }

    if (pid<0) 
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


int 
main (int argc, char *argv[])
{
    sigset_t mask;
    struct sigaction term, child;
    FILE *pf;
    GMainLoop *main_loop;    
 
    if (getuid()) {

	/* XDM compliant error message */
	fprintf (stderr, _("Only root wants to run x^hgdm\n"));

	exit (EXIT_FAILURE);
    }

    umask (022);
    gnome_do_not_create_directories = TRUE;
    gnomelib_init ("gdm", VERSION);
    main_loop = g_main_new (FALSE);
    openlog ("gdm", LOG_PID, LOG_DAEMON);

    gdm_config_parse();

    if (!access (GdmPidFile, R_OK)) {
        /* Check the process is still alive. Don't abort otherwise. */
        int pidv;

        pf = fopen (GdmPidFile, "r");
        if (pf && fscanf (pf, "%d", &pidv)==1 && kill (pidv,0)!=-1) {

        	fclose (pf);
		fprintf (stderr, _("gdm already running. Aborting!\n\n"));

		exit (EXIT_FAILURE);
	}

	fclose (pf);
	fprintf (stderr, _("According to %s, gdm was already running (%d),\n"
			   "but seems to have been murdered mysteriously.\n"), 
		 GdmPidFile, pidv);
	unlink (GdmPidFile);
    }

    /* Become daemon unless started in -nodaemon mode or child of init */
    if ( (argc==2 && strcmp (argv[1],"-nodaemon")==0) || getppid()==1) {
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

    return (EXIT_SUCCESS);
}

/* EOF */
