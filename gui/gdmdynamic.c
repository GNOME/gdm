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
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
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

#include "gdm.h"
#include "gdmcomm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"

static char *myname = NULL;  /* name of this program */

static void
usage()
{
    fprintf(stderr, "Usage: %s [-b][-v] and one of the following:\n", myname);
    fprintf(stderr, "\t-a display\n");
    fprintf(stderr, "\t-r display\n");
    fprintf(stderr, "\t-d display\n");
    fprintf(stderr, "\t-l [server_name]\n");
}


int
main (int argc, char *argv[])
{
    char *command = NULL;
    char *cstr;
    char *version;
    char *ret = NULL;
    char *params = "";
    int optc;
    int verbose = 0;
    int background = 0;
    gboolean error = TRUE;
    char *cookie = NULL;

    myname = basename(argv[0]);
    argv[0] = myname;

    version = "2.8.0.0";

    while ((optc = getopt(argc, argv, "a:d:r:blv")) != EOF) {
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

        case 'v':
            verbose++;
            break;

        case '?':
            error = TRUE;
        }
    }

    if (error) {
        usage();
        exit(1);
    }

    /* process remaining option arguments for -l */
    if (command == GDM_SUP_ATTACHED_SERVERS)
        for (; optind<argc; optind++)
            params = argv[optind];

    /* handle background option by forking and exiting the parent */

    if (background) {
        if ((background = fork()) != 0)  {
            if (background < 0)
                exit(1);
            else
                exit(0);
        }
    }

    gdmcomm_set_debug (verbose);

    if (params && strlen(params))
        cstr = g_strdup_printf ("%s %s", command, params);
    else
        cstr = g_strdup(command);

    /* All other commands besides LIST need root cookie */
    if (strcmp(command, GDM_SUP_ATTACHED_SERVERS) != 0) {
        char  *filename;
        gchar *GdmServAuthDir = NULL;
        FILE  *fp;
        char  buf[BUFSIZ];

        GdmServAuthDir = gdm_config_get_string (GDM_KEY_SERV_AUTHDIR);
        filename = g_build_filename (GdmServAuthDir, ".cookie", NULL);

        VE_IGNORE_EINTR (fp = fopen (filename, "r"));
        if (fp != NULL) {
            fgets(buf, sizeof(buf), fp);
            cookie = g_strdup(buf);
            VE_IGNORE_EINTR (fclose (fp));
        }
	g_free (filename);
    }

    ret = gdmcomm_call_gdm (cstr, cookie, version, 5);

    g_free(cstr);
    if (cookie)
        g_free(cookie);

    if (verbose && ret != NULL)
        fprintf(stderr,"%s\n", ret);

    if (ret != NULL &&
        strncmp (ret, "OK", 2) == 0) {

        if (strcmp(command, GDM_SUP_ATTACHED_SERVERS) == 0) {
            ret += 2;
            if(strlen(ret)) {
                ret++;   /* skip over space char */
                printf("%s\n", ret);
            }
        }

        /* all fine and dandy */
        return 0;
    }

    return 1;
}
