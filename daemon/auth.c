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

/* Code for cookie handling. This really needs to be modularized to
 * support other XAuth types and possibly DECnet... */

#include <config.h>
#include <gnome.h>
#include <sys/stat.h>
#include <netdb.h> 
#include <netinet/in.h>
#include <X11/Xauth.h>

#include "gdm.h"
#include "cookie.h"
#include "misc.h"
#include "filecheck.h"
#include "auth.h"

static const gchar RCSid[]="$Id$";


/* Local prototypes */
static void gdm_auth_purge (GdmDisplay *d, FILE *af);

/* Configuration option variables */
extern gchar *GdmServAuthDir;
extern gchar *GdmUserAuthDir;
extern gchar *GdmUserAuthFile;
extern gchar *GdmUserAuthFB;
extern gint  GdmUserMaxFile;
extern gint  GdmRelaxPerms;


/**
 * gdm_auth_secure_display:
 * @d: Pointer to a GdmDisplay struct
 * 
 * Create authentication cookies for local and remote displays.
 *
 * Returns TRUE on success and FALSE on error.
 */

gboolean
gdm_auth_secure_display (GdmDisplay *d)
{
    FILE *af;
    struct hostent *hentry;
    struct in_addr *ia;
    gchar *addr;
    Xauth *xa;
    guint i;
    gchar *hostname;

    if (!d)
	return FALSE;

    gdm_debug ("gdm_auth_secure_display: Setting up access for %s", d->name);

    if (! d->authfile)
	d->authfile = g_strconcat (GdmServAuthDir, "/", d->name, ".Xauth", NULL);
    
    unlink (d->authfile);

    af = fopen (d->authfile, "w");

    if (! af)
	return FALSE;

    /* If this is a local display the struct hasn't changed and we
     * have to eat up old authentication cookies before baking new
     * ones... */
    if (d->type == TYPE_LOCAL && d->auths) {
	GSList *alist = d->auths;

	while (alist && alist->data) {
	    XauDisposeAuth ((Xauth *) alist->data);
	    alist = alist->next;
	}

	g_slist_free (d->auths);
	d->auths = NULL;

	if (d->cookie)
	    g_free (d->cookie);

	if (d->bcookie)
	    g_free (d->bcookie);
    }

    /* Create new random cookie */
    gdm_cookie_generate (d);

    hostname = g_new0 (gchar, 1024);

    if (gethostname (hostname, 1023) == 0) {
        g_free( d->hostname );
        d->hostname = g_strdup( hostname );
    }
    g_free( hostname );

    /* Find FQDN or IP of display host */
    hentry = gethostbyname (d->hostname);

    if (!hentry) {
	gdm_error ("gdm_auth_secure_display: Error getting hentry for %s", d->hostname);
	return FALSE;
    }

    /* Local access */
    if (d->type == TYPE_LOCAL) {
	gdm_debug ("gdm_auth_secure_display: Setting up socket access");

	xa = g_new0 (Xauth, 1);
	
	if (!xa)
	    return FALSE;

	xa->family = FamilyLocal;
	xa->address = strdup (d->hostname);
	xa->address_length = strlen (d->hostname);
	xa->number = g_strdup_printf ("%d", d->dispnum);
	xa->number_length = 1;
	xa->name = strdup ("MIT-MAGIC-COOKIE-1");
	xa->name_length = 18;
	xa->data = strdup (d->bcookie);
	xa->data_length = strlen (d->bcookie);
	XauWriteAuth (af, xa);
	d->auths = g_slist_append (d->auths, xa);
    }

    gdm_debug ("gdm_auth_secure_display: Setting up network access");
    
    /* Network access: Write out an authentication entry for each of
     * this host's official addresses */
    for (i = 0 ; i < hentry->h_length ; i++) {
	xa = g_new0 (Xauth, 1);

	if (! xa)
	    return FALSE;

	xa->family = FamilyInternet;

	addr = g_new0 (gchar, 4);

	if (!addr)
	    return FALSE;

	ia = (struct in_addr *) hentry->h_addr_list[i];

	if (!ia)
	    break;

	memcpy (addr, &ia->s_addr, 4);
	xa->address = addr; 
	xa->address_length = 4;
	xa->number = g_strdup_printf ("%d", d->dispnum);
	xa->number_length = 1;
	xa->name = strdup ("MIT-MAGIC-COOKIE-1");
	xa->name_length = 18;
	xa->data = strdup (d->bcookie);
	xa->data_length = strlen (d->bcookie);

	XauWriteAuth (af, xa);

	d->auths = g_slist_append (d->auths, xa);
    }

    fclose (af);
    gdm_setenv ("XAUTHORITY", d->authfile);

    gdm_debug ("gdm_auth_secure_display: Setting up access for %s - %d entries", 
	       d->name, g_slist_length (d->auths));

    return TRUE;
}


/**
 * gdm_auth_user_add:
 * @d: Pointer to a GdmDisplay struct
 * @user: Userid of the user whose cookie file to add entries to
 * @homedir: The user's home directory
 * 
 * Remove all cookies referring to this display from user's cookie
 * file and append the ones specified in the display's authlist.
 *
 * Returns TRUE on success and FALSE on error.  
 */

gboolean
gdm_auth_user_add (GdmDisplay *d, uid_t user, gchar *homedir)
{
    gchar *authdir;
    gint authfd;
    FILE *af;
    GSList *auths = NULL;

    if (!d)
	return FALSE;

    gdm_debug ("gdm_auth_user_add: Adding cookie for %d", user);

    /* Determine whether UserAuthDir is specified. Otherwise ~user is used */
    if (*GdmUserAuthDir)
	authdir = GdmUserAuthDir;
    else
	authdir = homedir;

    umask (077);

    /* Find out if the Xauthority file passes the paranoia check */
    if (! gdm_file_check ("gdm_auth_user_add", user, authdir, GdmUserAuthFile, 
			  TRUE, GdmUserMaxFile, GdmRelaxPerms)) {

	/* No go. Let's create a fallback file in GdmUserAuthFB (/tmp) */
	d->authfb = TRUE;
	d->userauth = g_strconcat (GdmUserAuthFB, "/.gdmXXXXXX", NULL);
	authfd = mkstemp (d->userauth);

	if (authfd == -1) {
	    gdm_error (_("gdm_auth_user_add: Could not open cookie file %s"), d->userauth);
	    g_free (d->userauth);
	    d->userauth = NULL;

	    return FALSE;
	}

	af = fdopen (authfd, "w");
    }
    else { /* User's Xauthority file is ok */
	d->authfb = FALSE;
	d->userauth = g_strconcat (authdir, "/", GdmUserAuthFile, NULL);

	/* FIXME: Better implement my own locking. The libXau one is not kosher */
	if (XauLockAuth (d->userauth, 3, 3, 0) != LOCK_SUCCESS) {
	    gdm_error (_("gdm_auth_user_add: Could not lock cookie file %s"), d->userauth);
	    g_free (d->userauth);
	    d->userauth = NULL;

	    return FALSE;
	}

	af = fopen (d->userauth, "a+");
    }

    if (!af) {
	/* Really no need to clean up here - this process is a goner anyway */
	gdm_error (_("gdm_auth_user_add: Could not open cookie file %s"), d->userauth);
	XauUnlockAuth (d->userauth);
	g_free (d->userauth);
	d->userauth = NULL;

	return FALSE; 
    }

    gdm_debug ("gdm_auth_user_add: Using %s for cookies", d->userauth);

    /* If not a fallback file, nuke any existing cookies for this display */
    if (! d->authfb)
	gdm_auth_purge (d, af);

    /* Append the authlist for this display to the cookie file */
    auths = d->auths;

    while (auths) {
	XauWriteAuth (af, auths->data);
	auths = auths->next;
    }

    fclose (af);
    XauUnlockAuth (d->userauth);

    gdm_debug ("gdm_auth_user_add: Done");

    return TRUE;
}


/**
 * gdm_auth_user_remove:
 * @d: Pointer to a GdmDisplay struct
 * @user: Userid of the user whose cookie file to remove entries from
 * 
 * Remove all cookies referring to this display from user's cookie
 * file.
 */

void 
gdm_auth_user_remove (GdmDisplay *d, uid_t user)
{
    FILE *af;
    gchar *authfile, *authdir;

    if (!d || !d->userauth)
	return;

    gdm_debug ("gdm_auth_user_remove: Removing cookie from %s (%d)", d->userauth, d->authfb);

    /* If we are using the fallback cookie location, simply nuke the
     * cookie file */
    if (d->authfb) {
	unlink (d->userauth);
	g_free (d->userauth);
	d->userauth = NULL;
	return;
    }

    authfile = g_basename (d->userauth);
    authdir = g_dirname (d->userauth);

    /* Now, the cookie file could be owned by a malicious user who
     * decided to concatenate something like his entire MP3 collection
     * to it. So we better play it safe... */

    if (! gdm_file_check ("gdm_auth_user_remove", user, authdir, authfile, 
			  TRUE, GdmUserMaxFile, GdmRelaxPerms)) {
	gdm_error (_("gdm_auth_user_remove: Ignoring suspiciously looking cookie file %s"), d->userauth);

	return; 
    }

    g_free (authdir);

    /* Lock user's cookie jar and open it for writing */
    if (XauLockAuth (d->userauth, 3, 3, 0) != LOCK_SUCCESS)
	return;

    af = fopen (d->userauth, "a+");

    if (!af) {
	XauUnlockAuth (d->userauth);

	return;
    }

    /* Purge entries for this display from the cookie jar */
    gdm_auth_purge (d, af);

    /* Close the file and unlock it */
    fclose (af);
    XauUnlockAuth (d->userauth);

    g_free (d->userauth);
    d->userauth = NULL;

    return;
}


/**
 * gdm_auth_purge:
 * @d: Pointer to a GdmDisplay struct
 * @af: File handle to a cookie file
 * 
 * Remove all cookies referring to this display a cookie file.
 */

static void
gdm_auth_purge (GdmDisplay *d, FILE *af)
{
    Xauth *xa;
    GSList *keep = NULL;

    if (!d || !af)
	return;

    gdm_debug ("gdm_auth_purge: %s", d->name);

    fseek (af, 0L, SEEK_SET);

    /* Read the user's entire Xauth file into memory to avoid
     * temporary file issues. Then remove any instance of this display
     * in the cookie jar... */

    while ( (xa = XauReadAuth (af)) ) {
	gboolean match = FALSE;
	GSList *alist = d->auths;

	while (alist) {
	    Xauth *da = alist->data;

	    if (! memcmp (da->address, xa->address, xa->address_length) &&
		! memcmp (da->number, xa->number, xa->number_length))
		match = TRUE;

	    alist = alist->next;
	}

	if (match)
	    XauDisposeAuth (xa);
	else
	    keep = g_slist_append (keep, xa);
    }

    /* Rewind the file */
    af = freopen (d->userauth, "w", af);

    if (!af) {
	XauUnlockAuth (d->userauth);

	return;
    }

    /* Write out remaining entries */
    while (keep) {
	XauWriteAuth (af, keep->data);
	XauDisposeAuth (keep->data);
	keep = keep->next;
    }

    g_slist_free (keep);
}


/* EOF */
