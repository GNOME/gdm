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
#include <libgnome/libgnome.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <netdb.h> 
#include <netinet/in.h>
#include <X11/Xauth.h>

#include <vicious.h>

#include "gdm.h"
#include "cookie.h"
#include "misc.h"
#include "filecheck.h"
#include "auth.h"

/* Local prototypes */
static void gdm_auth_purge (GdmDisplay *d, FILE *af);

/* Configuration option variables */
extern gchar *GdmServAuthDir;
extern gchar *GdmUserAuthDir;
extern gchar *GdmUserAuthFile;
extern gchar *GdmUserAuthFB;
extern gint  GdmUserMaxFile;
extern gint  GdmRelaxPerms;
extern gboolean GdmDebug;

static void
display_add_error (GdmDisplay *d)
{
	if (errno != 0)
		gdm_error (_("%s: Could not write new authorization entry: %s"),
			   "add_auth_entry", g_strerror (errno));
	else
		gdm_error (_("%s: Could not write new authorization entry.  "
			     "Possibly out of diskspace"),
			   "add_auth_entry");
	if (d->console) {
		char *s = g_strdup_printf
			(_("GDM could not write a new authorization "
			   "entry to disk.  Possibly out of diskspace.%s%s"),
			 errno != 0 ? "  Error: " : "",
			 errno != 0 ? g_strerror (errno) : "");
		gdm_text_message_dialog (s);
		g_free (s);
	}
}

static gboolean
add_auth_entry (GdmDisplay *d, FILE *af, FILE *af2,
		unsigned short family, const char *addr, int addrlen)
{
	Xauth *xa;
	gchar *dispnum;

	if (!d)
		return FALSE;

	xa = malloc (sizeof (Xauth));

	if (xa == NULL)
		return FALSE;

	xa->family = family;
	xa->address = malloc (addrlen);
	if (xa->address == NULL) {
		free (xa);
		return FALSE;
	}


	memcpy (xa->address, addr, addrlen);
	xa->address_length = addrlen;

	dispnum = g_strdup_printf ("%d", d->dispnum);
	xa->number = strdup (dispnum);
	xa->number_length = strlen (dispnum);
	g_free (dispnum);

	xa->name = strdup ("MIT-MAGIC-COOKIE-1");
	xa->name_length = strlen ("MIT-MAGIC-COOKIE-1");
	xa->data = malloc (16);
	if (xa->data == NULL) {
		free (xa->number);
		free (xa->name);
		free (xa->address);
		free (xa);
		return FALSE;
	}
	memcpy (xa->data, d->bcookie, 16);
	xa->data_length = 16;

	errno = 0;
	if ( ! XauWriteAuth (af, xa)) {
		display_add_error (d);
		return FALSE;
	}

	if (af2 != NULL) {
		errno = 0;
		if ( ! XauWriteAuth (af2, xa)) {
			display_add_error (d);
			return FALSE;
		}
	}

	d->auths = g_slist_append (d->auths, xa);

	return TRUE;
}

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
    FILE *af, *af_gdm;
    struct hostent *hentry = NULL;
    struct in_addr *ia = NULL;
    const char lo[] = {127,0,0,1};
    guint i;
    const GList *local_addys = NULL;

    if (!d)
	return FALSE;

    umask (022);

    gdm_debug ("gdm_auth_secure_display: Setting up access for %s", d->name);

    g_free (d->authfile);
    d->authfile = NULL;
    g_free (d->authfile_gdm);
    d->authfile_gdm = NULL;

    if (d->server_uid != 0) {
	    int authfd;

	    /* Note, Xnest can't use the ServAuthDir unless running as
	     * root, which is rare anyway, unless the user is a wanker */

	    d->authfile = g_strconcat (GdmUserAuthFB, "/.gdmXXXXXX", NULL);

	    umask (077);
	    authfd = g_mkstemp (d->authfile);
	    umask (022);

	    if (authfd == -1) {
		    gdm_error (_("%s: Could not make new cookie file in %s"),
			       "gdm_auth_secure_display", GdmUserAuthFB);
		    g_free (d->authfile);
		    d->authfile = NULL;
		    return FALSE;
	    }

	    /* Make it owned by the user that Xnest is started as */
	    fchown (authfd, d->server_uid, -1);

	    af = fdopen (authfd, "w");

	    if (af == NULL) {
		    g_free (d->authfile);
		    d->authfile = NULL;
		    return FALSE;
	    }

	    /* Make another authfile since the greeter can't read the server/user
	     * readable file */
	    d->authfile_gdm = g_strconcat (GdmServAuthDir, "/", d->name, ".Xauth", NULL);
	    unlink (d->authfile_gdm);

	    af_gdm = fopen (d->authfile_gdm, "w");

	    if (af_gdm == NULL) {
		    g_free (d->authfile_gdm);
		    d->authfile_gdm = NULL;
		    g_free (d->authfile);
		    d->authfile = NULL;
		    fclose (af);
		    return FALSE;
	    }
    } else {
	    /* gdm and xserver authfile can be the same, server will run as root */
	    d->authfile = g_strconcat (GdmServAuthDir, "/", d->name, ".Xauth", NULL);
	    unlink (d->authfile);

	    af = fopen (d->authfile, "w");

	    if (af == NULL) {
		    g_free (d->authfile);
		    d->authfile = NULL;
		    return FALSE;
	    }

	    af_gdm = NULL;
    }

    /* If this is a local display the struct hasn't changed and we
     * have to eat up old authentication cookies before baking new
     * ones... */
    if (SERVER_IS_LOCAL (d) && d->auths) {
	    GSList *li;
	   
	    for (li = d->auths; li != NULL; li = li->next) {
		    XauDisposeAuth ((Xauth *) li->data);
		    li->data = NULL;
	    }

	    g_slist_free (d->auths);
	    d->auths = NULL;

	    g_free (d->cookie);
	    d->cookie = NULL;
	    g_free (d->bcookie);
	    d->bcookie = NULL;
    }

    /* Create new random cookie */
    gdm_cookie_generate (d);

    /* reget local host if local as it may have changed */
    if (SERVER_IS_LOCAL (d)) {
	    char hostname[1024];

	    hostname[1023] = '\0';
	    if (gethostname (hostname, 1023) == 0) {
		    g_free (d->hostname);
		    d->hostname = g_strdup (hostname);
	    }
	    local_addys = gdm_peek_local_address_list ();
    } else  {
	    /* Find FQDN or IP of display host */
	    hentry = gethostbyname (d->hostname);

	    if (hentry == NULL) {
		    gdm_error ("gdm_auth_secure_display: Error getting hentry for %s", d->hostname);
		    /* eeek, there will be nothing to add */
		    return FALSE;
	    } else {
		    /* first addy, would be loopback in case of local */
		    ia = (struct in_addr *) hentry->h_addr_list[0];
	    }
    }

    /* Local access also in case the host is very local */
    if (SERVER_IS_LOCAL (d) ||
	(ia != NULL && gdm_is_local_addr (ia))) {
	    gdm_debug ("gdm_auth_secure_display: Setting up socket access");

	    if ( ! add_auth_entry (d, af, af_gdm, FamilyLocal,
				   d->hostname, strlen (d->hostname)))
		    return FALSE;

	    /* local machine but not local if you get my meaning, add
	     * the host gotten by gethostname as well if it's different
	     * since the above is probably localhost */
	    if ( ! SERVER_IS_LOCAL (d)) {
		    char hostname[1024];

		    hostname[1023] = '\0';
		    if (gethostname (hostname, 1023) == 0 &&
			strcmp (hostname, d->hostname) != 0) {
			    if ( ! add_auth_entry (d, af, af_gdm, FamilyLocal,
						   hostname,
						   strlen (hostname)))
				    return FALSE;
		    }
	    } else {
		    /* local machine, perhaps we haven't added
		     * localhost.localdomain to socket access */
		    const char *localhost = "localhost.localdomain";
		    if (strcmp (localhost, d->hostname) != 0) {
			    if ( ! add_auth_entry (d, af, af_gdm, FamilyLocal,
						   localhost,
						   strlen (localhost))) {
				    return FALSE;
			    }
		    }
	    }
    }

    gdm_debug ("gdm_auth_secure_display: Setting up network access");
    
    /* Network access: Write out an authentication entry for each of
     * this host's official addresses */
    for (i = 0 ; hentry != NULL && i < hentry->h_length ; i++) {
	    ia = (struct in_addr *) hentry->h_addr_list[i];

	    if (ia == NULL)
		    break;

	    if ( ! add_auth_entry (d, af, af_gdm, FamilyInternet,
				   (char *)&ia->s_addr, 4))
		    return FALSE;
    }

    /* Network access: Write out an authentication entry for each of
     * this host's local addresses if any */
    for (; local_addys != NULL; local_addys = local_addys->next) {
	    ia = (struct in_addr *) local_addys->data;

	    if (ia == NULL)
		    break;

	    if ( ! add_auth_entry (d, af, af_gdm, FamilyInternet,
				   (char *)&ia->s_addr, 4))
		    return FALSE;
    }

    /* if local server add loopback */
    if (SERVER_IS_LOCAL (d)) {
	    if ( ! add_auth_entry (d, af, af_gdm, FamilyInternet,
				   lo, 4))
		    return FALSE;
    }

    fclose (af);
    if (af_gdm != NULL)
	    fclose (af_gdm);
    gnome_setenv ("XAUTHORITY", d->authfile, TRUE);

    if (GdmDebug)
	    gdm_debug ("gdm_auth_secure_display: Setting up access for %s - %d entries", 
		       d->name, g_slist_length (d->auths));

    return TRUE;
}

static gboolean
try_open_append (const char *file)
{
	FILE *fp;
	fp = fopen (file, "a+");
	if (fp != NULL) {
		fclose (fp);
		return TRUE;
	} else {
		return FALSE;
	}
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
gdm_auth_user_add (GdmDisplay *d, uid_t user, const char *homedir)
{
    char *authdir;
    gint authfd;
    FILE *af;
    GSList *auths = NULL;
    gboolean ret = TRUE;
    gboolean automatic_tmp_dir = FALSE;
    gboolean locked;

    if (!d)
	return FALSE;

    gdm_debug ("gdm_auth_user_add: Adding cookie for %d", user);

    /* Determine whether UserAuthDir is specified. Otherwise ~user is used */
    if ( ! ve_string_empty (GdmUserAuthDir) &&
	strcmp (GdmUserAuthDir, "~") != 0) {
	    if (strncmp (GdmUserAuthDir, "~/", 2) == 0) {
		    authdir = g_strconcat (homedir, &GdmUserAuthDir[1], NULL);
	    } else {
		    authdir = g_strdup (GdmUserAuthDir);
	    }
    } else {
	    authdir = g_strdup (homedir);
    }

try_user_add_again:

    locked = FALSE;

    umask (077);

    if (authdir == NULL)
	    d->userauth = NULL;
    else
	    d->userauth = g_strconcat (authdir, "/", GdmUserAuthFile, NULL);

    /* Find out if the Xauthority file passes the paranoia check */
    if (automatic_tmp_dir ||
	authdir == NULL ||
	! gdm_file_check ("gdm_auth_user_add", user, authdir, GdmUserAuthFile, 
			  TRUE, GdmUserMaxFile, GdmRelaxPerms) ||
	! try_open_append (d->userauth)) {

	/* No go. Let's create a fallback file in GdmUserAuthFB (/tmp) */
	d->authfb = TRUE;
	g_free (d->userauth);
	d->userauth = g_strconcat (GdmUserAuthFB, "/.gdmXXXXXX", NULL);
	authfd = g_mkstemp (d->userauth);

	if (authfd == -1) {
	    gdm_error (_("gdm_auth_user_add: Could not open cookie file %s"), d->userauth);
	    g_free (d->userauth);
	    d->userauth = NULL;

	    umask (022);

	    g_free (authdir);
	    return FALSE;
	}

	af = fdopen (authfd, "w");
    }
    else { /* User's Xauthority file is ok */
	d->authfb = FALSE;

	/* FIXME: Better implement my own locking. The libXau one is not kosher */
	if (XauLockAuth (d->userauth, 3, 3, 0) != LOCK_SUCCESS) {
	    gdm_error (_("gdm_auth_user_add: Could not lock cookie file %s"), d->userauth);
	    g_free (d->userauth);
	    d->userauth = NULL;

	    umask (022);

	    automatic_tmp_dir = TRUE;
	    goto try_user_add_again;
	}

	locked = TRUE;

	af = fopen (d->userauth, "a+");
    }

    if (!af) {
	/* Really no need to clean up here - this process is a goner anyway */
	gdm_error (_("gdm_auth_user_add: Could not open cookie file %s"), d->userauth);
	if (locked)
		XauUnlockAuth (d->userauth);
	g_free (d->userauth);
	d->userauth = NULL;

	umask (022);

	if ( ! d->authfb) {
		automatic_tmp_dir = TRUE;
		goto try_user_add_again;
	}

	g_free (authdir);

	return FALSE; 
    }

    gdm_debug ("gdm_auth_user_add: Using %s for cookies", d->userauth);

    /* If not a fallback file, nuke any existing cookies for this display */
    if (! d->authfb)
	gdm_auth_purge (d, af);

    /* Append the authlist for this display to the cookie file */
    auths = d->auths;

    while (auths) {
	if ( ! XauWriteAuth (af, auths->data)) {
		gdm_error (_("%s: Could not write cookie"),
			   "gdm_auth_user_add");

		if ( ! d->authfb) {
			fclose (af);
			if (locked)
				XauUnlockAuth (d->userauth);
			g_free (d->userauth);
			d->userauth = NULL;
			automatic_tmp_dir = TRUE;
			goto try_user_add_again;
		}

		ret = FALSE;
		break;
	}

	auths = auths->next;
    }

    fclose (af);
    if (locked)
	    XauUnlockAuth (d->userauth);

    gdm_debug ("gdm_auth_user_add: Done");

    umask (022);

    g_free (authdir);
    return ret;
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
    gchar *authfile;
    gchar *authdir;

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

    authfile = g_path_get_basename (d->userauth);
    authdir = g_path_get_dirname (d->userauth);

    /* Now, the cookie file could be owned by a malicious user who
     * decided to concatenate something like his entire MP3 collection
     * to it. So we better play it safe... */

    if ( ! gdm_file_check ("gdm_auth_user_remove", user, authdir, authfile, 
			   TRUE, GdmUserMaxFile, GdmRelaxPerms)) {
	    g_free (authdir);
	    g_free (authfile);
	    gdm_error (_("gdm_auth_user_remove: Ignoring suspiciously looking cookie file %s"), d->userauth);

	    return; 
    }

    g_free (authdir);
    g_free (authfile);

    /* Lock user's cookie jar and open it for writing */
    if (XauLockAuth (d->userauth, 3, 3, 0) != LOCK_SUCCESS) {
	g_free (d->userauth);
	d->userauth = NULL;
	return;
    }

    af = fopen (d->userauth, "a+");

    if (!af) {
	XauUnlockAuth (d->userauth);

	g_free (d->userauth);
	d->userauth = NULL;

	return;
    }

    /* Purge entries for this display from the cookie jar */
    gdm_auth_purge (d, af);

    /* Close the file and unlock it */
    fclose (af);
    XauUnlockAuth (d->userauth);

    g_free (d->userauth);
    d->userauth = NULL;
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
    GSList *keep = NULL, *li;

    if (!d || !af)
	return;

    gdm_debug ("gdm_auth_purge: %s", d->name);

    fseek (af, 0L, SEEK_SET);

    /* Read the user's entire Xauth file into memory to avoid
     * temporary file issues. Then remove any instance of this display
     * in the cookie jar... */

    while ( (xa = XauReadAuth (af)) != NULL ) {
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

    /* Write out remaining entries */
    for (li = keep; li != NULL; li = li->next) {
	    /* FIXME: is this correct, if we can't open
	     * this is quite crap isn't it ... */
	    if (af != NULL)
		    XauWriteAuth (af, li->data);
	    XauDisposeAuth (li->data);
	    li->data = NULL;
    }

    g_slist_free (keep);
}


/* EOF */
