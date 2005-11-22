/* GDM - The GNOME Display Manager
 * Copyright (C) 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2003 George Lebl
 * - Common routines for the greeters.
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

#include <unistd.h>
#include <dirent.h>
#include <syslog.h>

#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>

#include "vicious.h"

#include "gdm.h"
#include "gdmsession.h"
#include "gdmconfig.h"

GHashTable *sessnames        = NULL;
gchar *default_session       = NULL;
const gchar *current_session = NULL;
GList *sessions              = NULL;

/* This is true if session dir doesn't exist or is whacked out
 * in some way or another */
gboolean session_dir_whacked_out = FALSE;

gint
gdm_session_sort_func (const char *a, const char *b)
{
        /* Put default and GNOME sessions at the top */
        if (strcmp (a, ve_sure_string (gdm_config_get_string (GDM_KEY_DEFAULT_SESSION))) == 0)
                return -1;

        if (strcmp (b, ve_sure_string (gdm_config_get_string (GDM_KEY_DEFAULT_SESSION))) == 0)
                return 1;

        if (strcmp (a, "default.desktop") == 0)
                return -1;

        if (strcmp (b, "default.desktop") == 0)
                return 1;

        if (strcmp (a, "gnome.desktop") == 0)
                return -1;

        if (strcmp (b, "gnome.desktop") == 0)
                return 1;

        /* put failsafe sessions on the bottom */
        if (strcmp (b, GDM_SESSION_FAILSAFE_XTERM) == 0)
                return -1;

        if (strcmp (a, GDM_SESSION_FAILSAFE_XTERM) == 0)
                return 1;

        if (strcmp (b, GDM_SESSION_FAILSAFE_GNOME) == 0)
                return -1;

        if (strcmp (a, GDM_SESSION_FAILSAFE_GNOME) == 0)
                return 1;

        /* put everything else in the middle in alphabetical order */
                return strcmp (a, b);
}

const char *
gdm_session_name (const char *name)
{
        GdmSession *session;

        /* eek */
        if G_UNLIKELY (name == NULL)
                return "(null)";

        session = g_hash_table_lookup (sessnames, name);
        if (session != NULL && !ve_string_empty (session->name))
                return session->name;
        else
		return name;
}           

void
gdm_session_list_from_hash_table_func (const char *key, const char *value,
         GList **sessions)
{
        *sessions = g_list_prepend (*sessions, g_strdup (key));
}


void
gdm_session_list_init ()
{
    GdmSession *session = NULL;
    gboolean some_dir_exists = FALSE;
    gboolean searching_for_default = TRUE;
    gboolean show_xterm_failsafe;
    struct dirent *dent;
    char **vec;
    char *name;
    DIR *sessdir;
    int i;

    sessnames = g_hash_table_new (g_str_hash, g_str_equal);

    if (gdm_config_get_bool (GDM_KEY_SHOW_GNOME_FAILSAFE)) {
	session = g_new0 (GdmSession, 1);
	session->name = g_strdup (_("Failsafe _GNOME"));
	session->comment = g_strdup (_("This is a failsafe session that will log you "
		"into GNOME. No startup scripts will be read "
		"and it is only to be used when you can't log "
		"in otherwise.  GNOME will use the 'Default' "
		"session."));
	g_hash_table_insert (sessnames, g_strdup (GDM_SESSION_FAILSAFE_GNOME), session);
    }

    if (gdm_config_get_bool (GDM_KEY_SHOW_XTERM_FAILSAFE)) {
	session = g_new0 (GdmSession, 1);
	session->name = g_strdup (_("Failsafe _Terminal"));
	session->comment = g_strdup (_("This is a failsafe session that will log you "
		"into a terminal.  No startup scripts will be read "
		"and it is only to be used when you can't log "
		"in otherwise.  To exit the terminal, "
		"type 'exit'."));
	g_hash_table_insert (sessnames, g_strdup (GDM_SESSION_FAILSAFE_XTERM),
		session);
    }

    vec = g_strsplit (gdm_config_get_string (GDM_KEY_SESSION_DESKTOP_DIR),
	 ":", -1);
    for (i = 0; vec != NULL && vec[i] != NULL; i++) {
	    const char *dir = vec[i];

	    /* Check that session dir is readable */
	    if G_UNLIKELY (dir == NULL || access (dir, R_OK|X_OK) != 0)
		    continue;

	    some_dir_exists = TRUE;

	    /* Read directory entries in session dir */
	    sessdir = opendir (dir);

	    if G_LIKELY (sessdir != NULL)
		    dent = readdir (sessdir);
	    else
		    dent = NULL;

	    while (dent != NULL) {
		    VeConfig *cfg;
		    char *exec;
		    char *comment;
		    char *s;
		    char *tryexec;
		    char *ext;

		    /* ignore everything but the .desktop files */
		    ext = strstr (dent->d_name, ".desktop");
		    if (ext == NULL ||
			strcmp (ext, ".desktop") != 0) {
			    dent = readdir (sessdir);
			    continue;
		    }

		    /* already found this session, ignore */
		    if (g_hash_table_lookup (sessnames, dent->d_name) != NULL) {
			    dent = readdir (sessdir);
			    continue;
		    }

		    s = g_strconcat (dir, "/", dent->d_name, NULL);
		    cfg = ve_config_new (s);
		    g_free (s);

		    if (ve_config_get_bool (cfg, "Desktop Entry/Hidden=false")) {
			    session = g_new0 (GdmSession, 1);
			    session->name = "foo";
			    g_hash_table_insert (sessnames, g_strdup (dent->d_name), session);
			    ve_config_destroy (cfg);
			    dent = readdir (sessdir);
			    continue;
		    }

		    tryexec = ve_config_get_string (cfg, "Desktop Entry/TryExec");
		    if ( ! ve_string_empty (tryexec)) {
			    char *full = g_find_program_in_path (tryexec);
			    if (full == NULL) {
				    session = g_new0 (GdmSession, 1);
				    session->name = "foo";
				    g_hash_table_insert (sessnames, g_strdup (dent->d_name),
					 session);
				    g_free (tryexec);
				    ve_config_destroy (cfg);
				    dent = readdir (sessdir);
				    continue;
			    }
			    g_free (full);
		    }
		    g_free (tryexec);

		    exec = ve_config_get_string (cfg, "Desktop Entry/Exec");
		    name = ve_config_get_translated_string (cfg, "Desktop Entry/Name");
		    comment = ve_config_get_translated_string (cfg, "Desktop Entry/Comment");

		    ve_config_destroy (cfg);

		    if G_UNLIKELY (ve_string_empty (exec) || ve_string_empty (name)) {
			    session = g_new0 (GdmSession, 1);
			    session->name = "foo";
			    g_hash_table_insert (sessnames, g_strdup (dent->d_name), session);
			    g_free (exec);
			    g_free (name);
			    g_free (comment);
			    dent = readdir (sessdir);
			    continue;
		    }

		    /* if we found the default session */
		    if ( ! ve_string_empty (gdm_config_get_string (GDM_KEY_DEFAULT_SESSION)) &&
			 strcmp (dent->d_name, gdm_config_get_string (GDM_KEY_DEFAULT_SESSION)) == 0) {
			    g_free (default_session);
			    default_session = g_strdup (dent->d_name);
			    searching_for_default = FALSE;
		    }

		    /* if there is a session called Default */
		    if (searching_for_default &&
			g_ascii_strcasecmp (dent->d_name, "default.desktop") == 0) {
			    g_free (default_session);
			    default_session = g_strdup (dent->d_name);
		    }

		    if (searching_for_default &&
			g_ascii_strcasecmp (dent->d_name, "gnome.desktop") == 0) {
			    /* Just in case there is no default session and
			     * no default link, make gnome the default */
			    if (default_session == NULL)
				    default_session = g_strdup (dent->d_name);

		    }

		    session = g_new0 (GdmSession, 1);
		    session->name = g_strdup (name);
		    session->comment = g_strdup (comment);
		    g_hash_table_insert (sessnames, g_strdup (dent->d_name), session);
		    g_free (exec);
		    g_free (comment);
		    dent = readdir (sessdir);
	    }

	    if G_LIKELY (sessdir != NULL)
		    closedir (sessdir);
    }

    g_strfreev (vec);

    /* Check that session dir is readable */
    if G_UNLIKELY ( ! some_dir_exists) {
	syslog (LOG_ERR, _("%s: Session directory %s not found!"),
		"gdm_login_session_init", ve_sure_string
		 (gdm_config_get_string (GDM_KEY_SESSION_DESKTOP_DIR)));
	show_xterm_failsafe = TRUE;
	session_dir_whacked_out = TRUE;
    }

    if G_UNLIKELY (g_hash_table_size (sessnames) == 0) {
	    syslog (LOG_WARNING, _("Yikes, nothing found in the session directory."));
	    session_dir_whacked_out = TRUE;
	    show_xterm_failsafe = TRUE;
	    default_session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
    }

    if (gdm_config_get_bool (GDM_KEY_SHOW_XTERM_FAILSAFE))
	    show_xterm_failsafe = TRUE;

    if (show_xterm_failsafe) {
	    session = g_new0 (GdmSession, 1);
	    session->name = g_strdup (_("Failsafe _GNOME"));
	    session->comment = g_strdup (_("This is a failsafe session that will log you "
				    "into GNOME. No startup scripts will be read "
                                    "and it is only to be used when you can't log "
                                    "in otherwise.  GNOME will use the 'Default' "
				    "session."));
	    g_hash_table_insert (sessnames, g_strdup (GDM_SESSION_FAILSAFE_GNOME), session);
    }

    if (show_xterm_failsafe) {
	    session = g_new0 (GdmSession, 1);
	    session->name = g_strdup (_("Failsafe _Terminal"));
	    session->comment = g_strdup (_("This is a failsafe session that will log you "
				    "into a terminal.  No startup scripts will be read "
				    "and it is only to be used when you can't log "
				    "in otherwise.  To exit the terminal, "
				    "type 'exit'."));
	    g_hash_table_insert (sessnames, g_strdup (GDM_SESSION_FAILSAFE_XTERM), session);
    }

    /* Convert to list (which is unsorted) */
    g_hash_table_foreach (sessnames, (GHFunc) gdm_session_list_from_hash_table_func, &sessions);

    /* Prioritize and sort the list */
    sessions = g_list_sort (sessions, (GCompareFunc) gdm_session_sort_func);

    if G_UNLIKELY (default_session == NULL) {
	    default_session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
	    syslog (LOG_WARNING, _("No default session link found. Using Failsafe GNOME.\n"));
    }
    
    if (current_session == NULL)
            current_session = default_session;
}
