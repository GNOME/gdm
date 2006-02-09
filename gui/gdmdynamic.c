/*
 *    GDMcommunication routines
 *    (c)2001 Queen of England, (c)2002,2003 George Lebl
 *    
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *   
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *   
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 */

#include "config.h"
#include <gdk/gdkx.h>
#include <X11/Xauth.h>

#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>

#include <glib/gi18n.h>

#include "gdm.h"
#include "gdmcomm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"

static char *myname = NULL;  /* name of this program */

static void
usage ()
{
    fprintf (stderr, _("Usage: %s [-b][-v] and one of the following:\n"), myname);
    fprintf (stderr, _("\t-a display\n"));
    fprintf (stderr, _("\t-r display\n"));
    fprintf (stderr, _("\t-d display\n"));
    fprintf (stderr, _("\t-l [server_name]\n"));
    fprintf (stderr, _("\t-t maximum tries to connect (default 15)\n"));
    fprintf (stderr, _("\t-s sleep value (default 8)\n"));
}

/*
 * Note: gdmdynamic does work to deal with the socket being congested
 * because it is intended that the an umlimited number of gdmdynamic
 * requests could be made at any moment.  It is the responsibility of
 * gdmdynamic to manage the socket connection to the daemon to make sure
 * that it does not starve the socket, especially since the GUI login
 * program will also be using the socket on startup.
 */
int
main (int argc, char *argv[])
{
    gchar *cstr;
    gchar *version;
    gchar *params         = "";
    gchar *command        = NULL;
    gchar *ret            = NULL;
    gchar *cookie         = NULL;
    int optc;
    int try_num           = 0;
    int max_tries         = 15;
    int sleep_val         = 8;
    int verbose           = 0;
    int background        = 0;
    gboolean error        = TRUE;
    gboolean conn_failed  = FALSE;
    gboolean gdm_running;

    myname  = basename (argv[0]);
    argv[0] = myname;
    version = "2.8.0.0";

    while ((optc = getopt (argc, argv, "a:d:r:t:s:blv")) != EOF) {
        switch (optc) {
        case 'a':
            if (command == NULL)
                error = FALSE;
            command = GDM_SUP_ADD_DYNAMIC_DISPLAY;
            params = optarg;
            break;

        case 'b':
            background = 1;
            break;

        case 'd':
            if (command == NULL)
                error = FALSE;
            command = GDM_SUP_REMOVE_DYNAMIC_DISPLAY;
            params = optarg;
            break;

        case 'l':
            if (command == NULL)
                error = FALSE;
            command = GDM_SUP_ATTACHED_SERVERS;
            break;

        case 'r':
            if (command == NULL)
                error = FALSE;
            command = GDM_SUP_RELEASE_DYNAMIC_DISPLAYS;
            params = optarg;
            break;

        case 't':
            max_tries = atoi (optarg);
            break;

        case 's':
            sleep_val = atoi (optarg);
            break;

        case 'v':
            verbose++;
            break;

        case '?':
            error = TRUE;
        }
    }

    if (error) {
        usage ();
        exit (1);
    }

    /* process remaining option arguments for -l */
    if (command == GDM_SUP_ATTACHED_SERVERS)
        for (; optind<argc; optind++)
            params = argv[optind];

    /* handle background option by forking and exiting the parent */

    if (background) {
        if ((background = fork ()) != 0)  {
            if (background < 0)
                exit (1);
            else
                exit (0);
        }
    }

    gdmcomm_set_debug (verbose);
    /*
     * If verbose is not on, then set quiet errors to TRUE since
     * errors are expected and managed by gdmdynamic, so we want
     * errors to be quiet.
     */ 
    gdmcomm_set_quiet_errors (!verbose);

    if (params && strlen (params))
        cstr = g_strdup_printf ("%s %s", command, params);
    else
        cstr = g_strdup (command);

    /* Seed the random number generator in case we have to sleep. */
    srand (getpid () * time (NULL));

    /*
     * Setting comm_retries to 1 ensures that gdmdynamic will fail if
     * it fails the first time to connect.  We manage sleeping and
     * retrying in gdmdynamic instead of wanting gdmcomm to manage
     * this.
     */
    gdm_config_set_comm_retries (1);

    /*
     * Never cache config data because if it fails to connect, then
     * we want it to reload config data over the socket and not 
     * default to the compiled in value.
     */
    gdm_config_never_cache (TRUE);

    /*
     * If the connection is so busy that it fails, then we do not
     * want gdmdynamic to retry and further congest the connection.
     * Instead gdmdynamic will manage backing off and trying again
     * after waiting.
     */
    do {
	conn_failed = FALSE;
	try_num++;

	/*
	 * Verify server is not busy, sleep if it is.   No need to do this 
	 * check if doing LIST since this does not much much load on the
	 * daemon.
	 */
	if (strcmp (command, GDM_SUP_ATTACHED_SERVERS) != 0) {
		ret = gdmcomm_call_gdm (GDM_SUP_SERVER_BUSY, NULL, version, 1);
		conn_failed = gdmcomm_did_connection_fail ();
		if (ret == NULL)
			conn_failed = TRUE;
		else if (strncmp (ret, "OK", 2) == 0) {
			ret += 2;
			if (strlen (ret)) {
				ret++; /* skip over space char */
				if (strcmp (ret, "true") == 0) {
					conn_failed = TRUE;
					if (verbose)
						g_print (_("Server busy, will sleep.\n"));
				}
			}
		}
	}

	/*
	 * All other commands besides LIST need root cookie.  Only bother
         * getting the cookie if we haven't already gotten it since we do
         * this in a loop.
	 */
	if (conn_failed == FALSE &&
            (strcmp (command, GDM_SUP_ATTACHED_SERVERS) != 0) &&
            (cookie == NULL)) {
		gchar *GdmServAuthDir = NULL;
		char  *filename;
		FILE  *fp;
		char  buf[BUFSIZ];

		GdmServAuthDir = gdm_config_get_string (GDM_KEY_SERV_AUTHDIR);
		conn_failed    = gdmcomm_did_connection_fail ();

		/*
		 * We can't build a cookie if the auth dir is NULL.
		 * Since we only connect once, this could happen if
		 * the daemon closed the connection due to too many
		 * subconnections, for example.  So consider this
		 * getting a NULL value back a connection failed.
		 */
		if (ve_string_empty (GdmServAuthDir)) {
			conn_failed = TRUE;				
		}
		
		if (conn_failed == FALSE) {

			filename = g_build_filename (GdmServAuthDir, ".cookie", NULL);

			VE_IGNORE_EINTR (fp = fopen (filename, "r"));
			if (fp != NULL) {
				fgets (buf, sizeof (buf), fp);
				cookie = g_strdup (buf);
				VE_IGNORE_EINTR (fclose (fp));
			}
			g_free (filename);
		}
    	}

	if (conn_failed == FALSE) {
                /* Allow to fail if connection fails after 1 try */
		ret = gdmcomm_call_gdm (cstr, cookie, version, 1);

                /* Verify the connection did not fail */
		conn_failed = gdmcomm_did_connection_fail ();

		/*
		 * If the call returned NULL, there must be a failure
		 * talking to the daemon, consider this a connection
		 * failure.
		 */
		if (ret == NULL) {
			conn_failed = TRUE;
		}
	}

	/* Close socket now we are done using it. */
	gdmcomm_comm_close ();

	/*
         * If the connection failed, sleep and try again.  The sleep time is
         * somewhat random to ensure that if multiple calls to gdmdynamic 
         * all all failing to connect that they do not retry all at the
         * same time.
         */
	if (conn_failed == TRUE) {
		/* Sleep at least 1 second */
		int rand_sleep = 1;


		/*
		 * Make the sleep a bit random so if lots of calls hit the
		 * server at once, they are staggared.
		 */
		if (sleep_val > 0)
			rand_sleep = ((int) ((rand () % 10)/2)) + sleep_val;


		if (verbose) {
			g_print (_("Connection to daemon failed, sleeping for %d seconds.  Retry %d of %d\n"),
				 rand_sleep, try_num, max_tries);
		}

		sleep (rand_sleep);

		/* Reset the connection failed flag so it can be tried again */
		gdmcomm_set_allow_sleep (TRUE);
	}
    } while ((conn_failed == TRUE) && (try_num < max_tries));

    g_free (cstr);
    if (cookie)
        g_free (cookie);

    /*
     * If we failed to connect to the daemon after trying a certain number of
     * times, then return with code 2 to let the caller know that the failure
     * was due to connection problems.  The caller can decide to perhaps 
     * sleep a bit an try this command again.
     */
    if (conn_failed == TRUE) {
	/* This is a serious error, so print a message even if verbose is off */
	fprintf (stderr, _("Failed to connect to server after %d retries\n"),
		 try_num);
	return 2;
    }

    if (verbose && ret != NULL)
        g_print ("%s\n", ret);

    if (ret != NULL &&
        strncmp (ret, "OK", 2) == 0) {

        if (strcmp (command, GDM_SUP_ATTACHED_SERVERS) == 0) {
            ret += 2;
            if (strlen (ret)) {
                ret++;   /* skip over space char */
                g_print ("%s\n", ret);
            }
	}

	/* Success! */
        return 0;
    }

    return 1;
}
