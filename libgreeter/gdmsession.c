/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
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

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "gdmsession.h"

#include "gdm-common.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

GHashTable *sessnames        = NULL;
gchar *default_session       = NULL;
const gchar *current_session = NULL;
GList *sessions              = NULL;
static gint save_session     = GTK_RESPONSE_NO;


/* This is true if session dir doesn't exist or is whacked out
 * in some way or another */
gboolean session_dir_whacked_out = FALSE;

int
gdm_session_sort_func (const char *a, const char *b)
{
	int   ret;
	char *default_session;

        /* Put default and GNOME sessions at the top */
	default_session = NULL;
	if (! gdm_settings_client_get_string (GDM_KEY_DEFAULT_SESSION, &default_session)) {
		default_session = g_strdup ("default.desktop");
	}

        if (strcmp (a, default_session) == 0) {
		ret = -1;
		goto out;
	}

	if (strcmp (b, default_session) == 0) {
                ret = 1;
		goto out;
	}

        if (strcmp (a, "default.desktop") == 0) {
                ret = -1;
		goto out;
	}

        if (strcmp (b, "default.desktop") == 0) {
                ret = 1;
		goto out;
	}

        if (strcmp (a, "gnome.desktop") == 0) {
                ret = -1;
		goto out;
	}

        if (strcmp (b, "gnome.desktop") == 0) {
                ret = 1;
		goto out;
	}

        /* put failsafe sessions on the bottom */
	if (strcmp (b, GDM_SESSION_FAILSAFE_XTERM) == 0) {
                ret = -1;
		goto out;
	}

        if (strcmp (a, GDM_SESSION_FAILSAFE_XTERM) == 0) {
                ret = 1;
		goto out;
	}

        if (strcmp (b, GDM_SESSION_FAILSAFE_GNOME) == 0) {
                ret = -1;
		goto out;
	}

        if (strcmp (a, GDM_SESSION_FAILSAFE_GNOME) == 0) {
                ret = 1;
		goto out;
	}

        /* put everything else in the middle in alphabetical order */
	ret = strcmp (a, b);

 out:
	g_free (default_session);

	return ret;
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
gdm_session_list_from_hash_table_func (const char *key,
				       const char *value,
				       GList **sessions)
{
        *sessions = g_list_prepend (*sessions, g_strdup (key));
}

/* Just a wrapper to ensure compatibility with the
   existing code */
void
gdm_session_list_init ()
{
	_gdm_session_list_init (&sessnames,
				&sessions,
				&default_session,
				&current_session);
}

/* The real gdm_session_list_init */
void
_gdm_session_list_init (GHashTable **sessnames,
			GList **sessions,
			gchar **default_session,
			const gchar **current_session)
{

	GdmSession *session = NULL;
	gboolean some_dir_exists = FALSE;
	gboolean searching_for_default = TRUE;
	struct dirent *dent;
	char **vec;
	char *name;
	DIR *sessdir;
	gboolean show_failsafe;
	char *desktop_dir;
	int i;

	*sessnames = g_hash_table_new (g_str_hash, g_str_equal);

	show_failsafe = FALSE;
	gdm_settings_client_get_boolean (GDM_KEY_SHOW_GNOME_FAILSAFE, &show_failsafe);

	if (show_failsafe) {
		session = g_new0 (GdmSession, 1);
		session->name = g_strdup (_("Failsafe _GNOME"));
		session->clearname = g_strdup (_("Failsafe GNOME"));
		session->comment = g_strdup (_("This is a failsafe session that will log you "
					       "into GNOME. No startup scripts will be read "
					       "and it is only to be used when you can't log "
					       "in otherwise.  GNOME will use the 'Default' "
					       "session."));
		g_hash_table_insert (*sessnames, g_strdup (GDM_SESSION_FAILSAFE_GNOME), session);
	}

	show_failsafe = FALSE;
	gdm_settings_client_get_boolean (GDM_KEY_SHOW_XTERM_FAILSAFE, &show_failsafe);

	if (show_failsafe) {
		/* Valgrind complains that the below is leaked */
		session = g_new0 (GdmSession, 1);
		session->name = g_strdup (_("Failsafe _Terminal"));
		session->clearname = g_strdup (_("Failsafe Terminal"));
		session->comment = g_strdup (_("This is a failsafe session that will log you "
					       "into a terminal.  No startup scripts will be read "
					       "and it is only to be used when you can't log "
					       "in otherwise.  To exit the terminal, "
					       "type 'exit'."));
		g_hash_table_insert (*sessnames, g_strdup (GDM_SESSION_FAILSAFE_XTERM),
				     session);
	}

	gdm_settings_client_get_string (GDM_KEY_SESSION_DESKTOP_DIR, &desktop_dir);
	vec = g_strsplit (desktop_dir, ":", -1);

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
			GKeyFile *cfg;
			char *exec;
			char *comment;
			char *s;
			char *tryexec;
			char *ext;
			gboolean hidden;

			/* ignore everything but the .desktop files */
			ext = strstr (dent->d_name, ".desktop");
			if (ext == NULL ||
			    strcmp (ext, ".desktop") != 0) {
				dent = readdir (sessdir);
				continue;
			}

			/* already found this session, ignore */
			if (g_hash_table_lookup (*sessnames, dent->d_name) != NULL) {
				dent = readdir (sessdir);
				continue;
			}

			s = g_strconcat (dir, "/", dent->d_name, NULL);
			cfg = g_key_file_new ();
			g_key_file_load_from_file (cfg, s, 0, NULL);
			g_free (s);

			hidden = g_key_file_get_boolean (cfg, "Desktop Entry", "Hidden", NULL);
			if (hidden) {
				session = g_new0 (GdmSession, 1);
				session->name      = g_strdup (dent->d_name);
				session->clearname = NULL;
				g_hash_table_insert (*sessnames, g_strdup (dent->d_name), session);
				g_key_file_free (cfg);
				dent = readdir (sessdir);
				continue;
			}

			tryexec = g_key_file_get_string (cfg, "Desktop Entry", "TryExec", NULL);
			if ( ! ve_string_empty (tryexec)) {
				char **tryexecvec = g_strsplit (tryexec, " ", -1);
				char *full = NULL;

				/* Do not pass any arguments to g_find_program_in_path */
				if (tryexecvec != NULL)
					full = g_find_program_in_path (tryexecvec[0]);

				if (full == NULL) {
					session = g_new0 (GdmSession, 1);
					session->name      = g_strdup (dent->d_name);
					session->clearname = NULL;
					g_hash_table_insert (*sessnames, g_strdup (dent->d_name),
							     session);
					g_free (tryexec);
					g_key_file_free (cfg);
					dent = readdir (sessdir);
					continue;
				}
				g_strfreev (tryexecvec);
				g_free (full);
			}
			g_free (tryexec);

			exec = g_key_file_get_string (cfg, "Desktop Entry", "Exec", NULL);
			name = g_key_file_get_locale_string (cfg, "Desktop Entry", "Name", NULL, NULL);
			comment = g_key_file_get_locale_string (cfg, "Desktop Entry", "Comment", NULL, NULL);
			g_key_file_free (cfg);

			if G_UNLIKELY (ve_string_empty (exec) || ve_string_empty (name)) {
				session = g_new0 (GdmSession, 1);
				session->name      = g_strdup (dent->d_name);
				session->clearname = NULL;
				g_hash_table_insert (*sessnames, g_strdup (dent->d_name), session);
				g_free (exec);
				g_free (name);
				g_free (comment);
				dent = readdir (sessdir);
				continue;
			}

			/* if we found the default session */
			if (default_session != NULL) {
				char *def;

				def = NULL;
				gdm_settings_client_get_string (GDM_KEY_DEFAULT_SESSION, &def);
				if ( ! ve_string_empty (def) && strcmp (dent->d_name, def) == 0) {
					g_free (*default_session);
					*default_session = g_strdup (dent->d_name);
					searching_for_default = FALSE;
				}
				g_free (def);

				/* if there is a session called Default */
				if (searching_for_default &&
				    g_ascii_strcasecmp (dent->d_name, "default.desktop") == 0) {
					g_free (*default_session);
					*default_session = g_strdup (dent->d_name);
				}

				if (searching_for_default &&
				    g_ascii_strcasecmp (dent->d_name, "gnome.desktop") == 0) {
					/* Just in case there is no default session and
					 * no default link, make gnome the default */
					if (*default_session == NULL)
						*default_session = g_strdup (dent->d_name);

				}
			}

			session = g_new0 (GdmSession, 1);
			session->name      = g_strdup (name);
			session->clearname = NULL;
			session->comment   = g_strdup (comment);
			g_hash_table_insert (*sessnames, g_strdup (dent->d_name), session);
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
		g_warning ("%s: Session directory <%s> not found!",
			   "gdm_session_list_init",
			   desktop_dir);
		session_dir_whacked_out = TRUE;
	}

	if G_UNLIKELY (g_hash_table_size (*sessnames) == 0) {
		g_warning ("Error, no sessions found in the session directory <%s>.",
			   desktop_dir);

		session_dir_whacked_out = TRUE;
		if (default_session != NULL)
			*default_session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
	}

	g_free (desktop_dir);

	/* Convert to list (which is unsorted) */
	g_hash_table_foreach (*sessnames,
			      (GHFunc) gdm_session_list_from_hash_table_func, sessions);

	/* Prioritize and sort the list */
	*sessions = g_list_sort (*sessions, (GCompareFunc) gdm_session_sort_func);

	if (default_session != NULL)
		if G_UNLIKELY (*default_session == NULL) {
			*default_session = g_strdup (GDM_SESSION_FAILSAFE_GNOME);
			g_warning ("No default session link found. Using Failsafe GNOME.");
		}

	if (current_session != NULL &&
	    default_session != NULL) {
		if (*current_session == NULL)
			*current_session = *default_session;
	}
}

static gboolean
gdm_login_list_lookup (GList *l,
		       const gchar *data)
{
	GList *list = l;

	if (list == NULL || data == NULL)
		return FALSE;

	/* FIXME: Hack, will support these builtin types later */
	if (strcmp (data, GDM_SESSION_DEFAULT ".desktop") == 0 ||
	    strcmp (data, GDM_SESSION_CUSTOM ".desktop") == 0 ||
	    strcmp (data, GDM_SESSION_FAILSAFE ".desktop") == 0) {
		return TRUE;
	}

	while (list) {

		if (strcmp (list->data, data) == 0)
			return TRUE;

		list = list->next;
	}

	return FALSE;
}

char *
gdm_session_lookup (const char *saved_session,
		    gint *lookup_status)
{
	gchar *session = NULL;

	/* Assume that the lookup will go well */
	*lookup_status = SESSION_LOOKUP_SUCCESS;

	/* Don't save session unless told otherwise */
	save_session = GTK_RESPONSE_NO;

	/* Previously saved session not found in ~/.dmrc */
	if ( ! (saved_session != NULL &&
		strcmp ("(null)", saved_session) != 0 &&
		saved_session[0] != '\0')) {
		/* If "Last" is chosen run default,
		 * else run user's current selection */
		if (current_session == NULL || strcmp (current_session, LAST_SESSION) == 0)
			session = g_strdup (default_session);
		else
			session = g_strdup (current_session);

		save_session = GTK_RESPONSE_YES;
		return session;
	}

	/* If "Last" session is selected */
	if (current_session == NULL || strcmp (current_session, LAST_SESSION) == 0) {
		session = g_strdup (saved_session);

		/* Check if user's saved session exists on this box */
		if (!gdm_login_list_lookup (sessions, session)) {
			g_free (session);
			session = g_strdup (default_session);
			*lookup_status = SESSION_LOOKUP_PREFERRED_MISSING;
		}
	} else {
		/* One of the other available session types is selected */
		session = g_strdup (current_session);

		/* User's saved session is not the chosen one */
		if (strcmp (session, GDM_SESSION_FAILSAFE_GNOME) == 0 ||
		    strcmp (session, GDM_SESSION_FAILSAFE_XTERM) == 0 ||
		    g_ascii_strcasecmp (session, GDM_SESSION_FAILSAFE ".desktop") == 0 ||
		    g_ascii_strcasecmp (session, GDM_SESSION_FAILSAFE) == 0) {
			/*
			 * Never save failsafe sessions as the default session.
			 * These are intended to be used for debugging or temporary
			 * purposes.
			 */
			save_session = GTK_RESPONSE_NO;
		} else if (strcmp (saved_session, session) != 0) {
			gboolean show_last;

			show_last = FALSE;
			gdm_settings_client_get_boolean (GDM_KEY_SHOW_LAST_SESSION, &show_last);

			if (show_last) {
				*lookup_status = SESSION_LOOKUP_DEFAULT_MISMATCH;
			} else if (strcmp (session, default_session) != 0 &&
				   strcmp (session, saved_session) != 0 &&
				   strcmp (session, LAST_SESSION) != 0) {
				/*
				 * If (! GDM_KEY_SHOW_LAST_SESSION) then our saved session is
				 * irrelevant, we are in "switchdesk mode" and the relevant
				 * thing is the saved session in .Xclients
				 */
				if (g_access ("/usr/bin/switchdesk", F_OK) == 0) {
					*lookup_status = SESSION_LOOKUP_USE_SWITCHDESK;
				}
				save_session = GTK_RESPONSE_NO;
			}
		}
	}

	return session;
}

gint
gdm_get_save_session (void)
{
	return save_session;
}

void
gdm_set_save_session (const gint session)
{
	save_session = session;
}

const char*
gdm_get_default_session (void)
{
	return default_session;
}
