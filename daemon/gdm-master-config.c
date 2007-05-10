/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h> /* for PRIO_MIN */
#include <ctype.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "gdm-config.h"
#include "gdm-master-config.h"
#include "gdm-daemon-config-entries.h"

#define GDM_STANDARD "Standard"

/* PRIO_MIN and PRIO_MAX are not defined on Solaris, but are -20 and 20 */
#if sun
#ifndef PRIO_MIN
#define PRIO_MIN -20
#endif
#ifndef PRIO_MAX
#define PRIO_MAX 20
#endif
#endif

#define GDM_DAEMON_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_DAEMON_CONFIG, GdmDaemonConfigPrivate))

struct GdmDaemonConfigPrivate
{
	GdmConfig *config;
	GSList    *xservers;

	char      *default_file;
	char      *custom_file;
};

static void	gdm_daemon_config_class_init	(GdmDaemonConfigClass *klass);
static void	gdm_daemon_config_init	        (GdmDaemonConfig      *daemon_config);
static void	gdm_daemon_config_finalize	(GObject	      *object);

static gpointer daemon_config_object = NULL;

G_DEFINE_TYPE (GdmDaemonConfig, gdm_daemon_config, G_TYPE_OBJECT)

GQuark
gdm_daemon_config_error_quark (void)
{
	static GQuark ret = 0;
	if (ret == 0) {
		ret = g_quark_from_static_string ("gdm_daemon_config_error");
	}

	return ret;
}

/*
 * gdm_read_default
 *
 * This function is used to support systems that have the /etc/default/login
 * interface to control programs that affect security.  This is a Solaris
 * thing, though some users on other systems may find it useful.
 */
static char *
gdm_read_default (char *key)
{
#ifdef HAVE_DEFOPEN
    gchar *retval = NULL;

    if (defopen ("/etc/default/login") == 0) {
       int flags = defcntl (DC_GETFLAGS, 0);

       TURNOFF (flags, DC_CASE);
       (void) defcntl (DC_SETFLAGS, flags);  /* ignore case */
       retval = g_strdup (defread (key));
       (void) defopen ((char *)NULL);
    }
    return retval;
#else
    return NULL;
#endif
}

/**
 * gdm_daemon_config_get_bool_for_id
 *
 * Gets a boolean configuration option by ID.  The option must
 * first be loaded, say, by calling gdm_daemon_config_parse.
 */
gboolean
gdm_daemon_config_get_bool_for_id (GdmDaemonConfig *config,
				   int              id,
				   gboolean        *val)
{
	return gdm_config_get_bool_for_id (config->priv->config, id, val);
}

/**
 * gdm_daemon_config_get_int_for_id
 *
 * Gets a integer configuration option by ID.  The option must
 * first be loaded, say, by calling gdm_daemon_config_parse.
 */
gboolean
gdm_daemon_config_get_int_for_id (GdmDaemonConfig *config,
				  int              id,
				  int             *val)
{
	return gdm_config_get_int_for_id (config->priv->config, id, val);
}

/**
 * gdm_daemon_config_get_string_for_id
 *
 * Gets a string configuration option by ID.  The option must
 * first be loaded, say, by calling gdm_daemon_config_parse.
 */
gboolean
gdm_daemon_config_get_string_for_id (GdmDaemonConfig *config,
				     int              id,
				     char           **val)
{
	return gdm_config_get_string_for_id (config->priv->config, id, val);
}

static void
gdm_daemon_config_class_init (GdmDaemonConfigClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gdm_daemon_config_finalize;

	g_type_class_add_private (klass, sizeof (GdmDaemonConfigPrivate));
}

static gboolean
validate_path (GdmConfig          *config,
	       GdmConfigSourceType source,
	       GdmConfigValue     *value)
{
	char    *str;

	/* If the /etc/default has a PATH use that */
	str = gdm_read_default ("PATH=");
	if (str != NULL) {
		gdm_config_value_set_string (value, str);
		g_free (str);
	}

	return TRUE;
}

static gboolean
validate_root_path (GdmConfig          *config,
		    GdmConfigSourceType source,
		    GdmConfigValue     *value)
{
	char    *str;

	/* If the /etc/default has a PATH use that */
	str = gdm_read_default ("SUPATH=");
	if (str != NULL) {
		gdm_config_value_set_string (value, str);
		g_free (str);
	}

	return TRUE;
}

static gboolean
validate_base_xsession (GdmConfig          *config,
			GdmConfigSourceType source,
			GdmConfigValue     *value)
{
	const char *str;

	str = gdm_config_value_get_string (value);
	if (str == NULL || str[0] == '\0') {
		char *path;
		path = g_build_filename (GDMCONFDIR, "gdm", "Xsession", NULL);
		g_debug (_("BaseXsession empty; using %s"), path);
		gdm_config_value_set_string (value, path);
		g_free (path);
	}

	return TRUE;
}

static gboolean
validate_power_action (GdmConfig          *config,
		       GdmConfigSourceType source,
		       GdmConfigValue     *value)
{
	/* FIXME: should weed out the commands that don't work */

	return TRUE;
}

static gboolean
validate_standard_xserver (GdmConfig          *config,
			   GdmConfigSourceType source,
			   GdmConfigValue     *value)
{
	gboolean    res;
	gboolean    is_ok;
	const char *str;
	char       *new;

	is_ok = FALSE;
	new = NULL;
	str = gdm_config_value_get_string (value);

	if (str != NULL) {
		char **argv;

		if (g_shell_parse_argv (str, NULL, &argv, NULL)) {
			if (g_access (argv[0], X_OK) == 0) {
				is_ok = TRUE;
			}
			g_strfreev (argv);
		}
	}

	if G_UNLIKELY (! is_ok) {
		g_debug (_("Standard X server not found; trying alternatives"));
		if (g_access ("/usr/X11R6/bin/X", X_OK) == 0) {
			new = g_strdup ("/usr/X11R6/bin/X");
		} else if (g_access ("/opt/X11R6/bin/X", X_OK) == 0) {
			new = g_strdup ("/opt/X11R6/bin/X");
		} else if (g_access ("/usr/bin/X11/X", X_OK) == 0) {
			new = g_strdup ("/usr/bin/X11/X");
		}
	}

	if (new != NULL) {
		gdm_config_value_set_string (value, new);
		g_free (new);
	}

	res = TRUE;

	return res;
}

static gboolean
validate_graphical_theme_dir (GdmConfig          *config,
			      GdmConfigSourceType source,
			      GdmConfigValue     *value)
{
	const char *str;

	str = gdm_config_value_get_string (value);

	if (str == NULL || !g_file_test (str, G_FILE_TEST_IS_DIR)) {
		gdm_config_value_set_string (value, GREETERTHEMEDIR);
	}

	return TRUE;
}

static gboolean
validate_graphical_theme (GdmConfig          *config,
			  GdmConfigSourceType source,
			  GdmConfigValue     *value)
{
	const char *str;

	str = gdm_config_value_get_string (value);

	if (str == NULL || str[0] == '\0') {
		gdm_config_value_set_string (value, "circles");
	}

	return TRUE;
}

static gboolean
validate_greeter (GdmConfig          *config,
		  GdmConfigSourceType source,
		  GdmConfigValue     *value)
{
	const char *str;

	str = gdm_config_value_get_string (value);

	if (str == NULL || str[0] == '\0') {
		g_warning (_("%s: No greeter specified."), "gdm_config_parse");
	}

	return TRUE;
}

static gboolean
validate_remote_greeter (GdmConfig          *config,
			 GdmConfigSourceType source,
			 GdmConfigValue     *value)
{
	const char *str;

	str = gdm_config_value_get_string (value);

	if (str == NULL || str[0] == '\0') {
		g_warning (_("%s: No remote greeter specified."), "gdm_config_parse");
	}

	return TRUE;
}

static gboolean
validate_session_desktop_dir (GdmConfig          *config,
			      GdmConfigSourceType source,
			      GdmConfigValue     *value)
{
	const char *str;

	str = gdm_config_value_get_string (value);

	if (str == NULL || str[0] == '\0') {
		g_warning (_("%s: No sessions directory specified."), "gdm_config_parse");
	}

	return TRUE;
}

static gboolean
validate_password_required (GdmConfig          *config,
			    GdmConfigSourceType source,
			    GdmConfigValue     *value)
{
	char *str;

	str = gdm_read_default ("PASSREQ=");
	if (str != NULL && str[0] == '\0') {
		gboolean val;
		val = (g_ascii_strcasecmp (str, "YES") == 0);
		gdm_config_value_set_bool (value, val);
	}

	return TRUE;
}

static gboolean
validate_allow_remote_root (GdmConfig          *config,
			    GdmConfigSourceType source,
			    GdmConfigValue     *value)
{
	char *str;

	str = gdm_read_default ("CONSOLE=");
	if (str != NULL && str[0] == '\0') {
		gboolean val;
		val = (g_ascii_strcasecmp (str, "/dev/console") != 0);
		gdm_config_value_set_bool (value, val);
	}

	return TRUE;
}

static gboolean
validate_xdmcp (GdmConfig          *config,
		GdmConfigSourceType source,
		GdmConfigValue     *value)
{

#ifndef HAVE_LIBXDMCP
	if (gdm_config_value_get_bool (value)) {
		g_debug (_("XDMCP was enabled while there is no XDMCP support; turning it off"));
		gdm_config_value_set_bool (value, FALSE);
	}
#endif

	return TRUE;
}

static gboolean
validate_at_least_int (GdmConfig          *config,
		       GdmConfigSourceType source,
		       GdmConfigValue     *value,
		       int                 minval,
		       int                 defval)
{
	if (gdm_config_value_get_int (value) < minval) {
		gdm_config_value_set_int (value, defval);
	}

	return TRUE;
}

static gboolean
validate_cb (GdmConfig          *config,
	     GdmConfigSourceType source,
	     const char         *group,
	     const char         *key,
	     GdmConfigValue     *value,
	     int                 id,
	     gpointer            data)
{
	gboolean res;

	res = TRUE;

        switch (id) {
        case GDM_ID_PATH:
		res = validate_path (config, source, value);
		break;
        case GDM_ID_ROOT_PATH:
		res = validate_root_path (config, source, value);
		break;
        case GDM_ID_BASE_XSESSION:
		res = validate_base_xsession (config, source, value);
		break;
        case GDM_ID_HALT:
        case GDM_ID_REBOOT:
        case GDM_ID_SUSPEND:
		res = validate_power_action (config, source, value);
		break;
        case GDM_ID_STANDARD_XSERVER:
		res = validate_standard_xserver (config, source, value);
		break;
        case GDM_ID_GRAPHICAL_THEME_DIR:
		res = validate_graphical_theme_dir (config, source, value);
		break;
        case GDM_ID_GRAPHICAL_THEME:
		res = validate_graphical_theme (config, source, value);
		break;
        case GDM_ID_GREETER:
		res = validate_greeter (config, source, value);
		break;
        case GDM_ID_REMOTE_GREETER:
		res = validate_remote_greeter (config, source, value);
		break;
        case GDM_ID_SESSION_DESKTOP_DIR:
		res = validate_session_desktop_dir (config, source, value);
		break;
        case GDM_ID_PASSWORD_REQUIRED:
		res = validate_password_required (config, source, value);
		break;
        case GDM_ID_ALLOW_REMOTE_ROOT:
		res = validate_allow_remote_root (config, source, value);
		break;
        case GDM_ID_XDMCP:
		res = validate_xdmcp (config, source, value);
		break;
	case GDM_ID_MAX_INDIRECT:
	case GDM_ID_XINERAMA_SCREEN:
		res = validate_at_least_int (config, source, value, 0, 0);
		break;
	case GDM_ID_TIMED_LOGIN_DELAY:
		res = validate_at_least_int (config, source, value, 5, 5);
		break;
	case GDM_ID_MAX_ICON_WIDTH:
	case GDM_ID_MAX_ICON_HEIGHT:
		res = validate_at_least_int (config, source, value, 0, 128);
		break;
	case GDM_ID_SCAN_TIME:
		res = validate_at_least_int (config, source, value, 1, 1);
		break;
        case GDM_ID_NONE:
        case GDM_CONFIG_INVALID_ID:
		break;
	default:
		break;
	}

	return res;
}

static const char *
source_to_name (GdmConfigSourceType source)
{
        const char *name;

        switch (source) {
        case GDM_CONFIG_SOURCE_DEFAULT:
                name = "default";
                break;
        case GDM_CONFIG_SOURCE_MANDATORY:
                name = "mandatory";
                break;
        case GDM_CONFIG_SOURCE_CUSTOM:
                name = "custom";
                break;
        case GDM_CONFIG_SOURCE_BUILT_IN:
                name = "built-in";
                break;
        case GDM_CONFIG_SOURCE_RUNTIME_USER:
                name = "runtime-user";
                break;
        case GDM_CONFIG_SOURCE_INVALID:
                name = "Invalid";
                break;
        default:
                name = "Unknown";
                break;
        }

        return name;
}

static gboolean
notify_cb (GdmConfig          *config,
	   GdmConfigSourceType source,
	   const char         *group,
	   const char         *key,
	   GdmConfigValue     *value,
	   int                 id,
	   gpointer            data)
{
	char *valstr;

	/* FIXME: need a better approach */
#if 0
        switch (id) {
        case GDM_ID_GREETER:
        case GDM_ID_REMOTE_GREETER:
        case GDM_ID_SOUND_ON_LOGIN_FILE:
        case GDM_ID_SOUND_ON_LOGIN_SUCCESS_FILE:
        case GDM_ID_SOUND_ON_LOGIN_FAILURE_FILE:
        case GDM_ID_GTK_MODULES_LIST:
        case GDM_ID_TIMED_LOGIN:
        case GDM_ID_ALLOW_ROOT:
        case GDM_ID_ALLOW_REMOTE_ROOT:
        case GDM_ID_ALLOW_REMOTE_AUTOLOGIN:
        case GDM_ID_SYSTEM_MENU:
        case GDM_ID_CONFIG_AVAILABLE:
        case GDM_ID_CHOOSER_BUTTON:
        case GDM_ID_DISALLOW_TCP:
        case GDM_ID_ADD_GTK_MODULES:
        case GDM_ID_TIMED_LOGIN_ENABLE:
	case GDM_ID_RETRY_DELAY:
	case GDM_ID_TIMED_LOGIN_DELAY:
		notify_displays_value (config, group, key, value);
		break;
        case GDM_ID_NONE:
        case GDM_CONFIG_INVALID_ID:
		{
			/* doesn't have an ID : match group/key */
			if (group != NULL) {
				if (strcmp (group, GDM_CONFIG_GROUP_SERVERS) == 0) {
					/* FIXME: handle this? */
				} else if (strcmp (group, GDM_CONFIG_GROUP_CUSTOM_CMD) == 0) {
					notify_displays_value (config, group, key, value);
				}
			}
		}
                break;
	default:
		break;
        }
#endif

	valstr = gdm_config_value_to_string (value);
	g_debug ("Got config %s/%s=%s <%s>\n",
		 group,
		 key,
		 valstr,
		 source_to_name (source));
	g_free (valstr);

        return TRUE;
}

/**
 * gdm_daemon_config_find_xserver
 *
 * Return an xserver with a given ID, or NULL if not found.
 */
GdmXserver *
gdm_daemon_config_find_xserver (GdmDaemonConfig *config,
				const char      *id)
{
	GSList *li;

	if (config->priv->xservers == NULL) {
		return NULL;
	}

	if (id == NULL) {
		return config->priv->xservers->data;
	}

	for (li = config->priv->xservers; li != NULL; li = li->next) {
		GdmXserver *svr = li->data;
		if (svr->id != NULL && strcmp (svr->id, id) == 0) {
			return svr;
		}
	}

	return NULL;
}

/**
 * gdm_daemon_config_get_xservers
 *
 * Prepare a string to be returned for the GET_SERVER_LIST
 * sockets command.
 */
gchar *
gdm_daemon_config_get_xservers (GdmDaemonConfig *config)
{
	GSList *li;
	char   *retval = NULL;

	if (config->priv->xservers == NULL) {
		return NULL;
	}

	for (li = config->priv->xservers; li != NULL; li = li->next) {
		GdmXserver *svr = li->data;
		if (retval != NULL)
			retval = g_strconcat (retval, ";", svr->id, NULL);
		else
			retval = g_strdup (svr->id);
	}

	return retval;
}

/**
 * gdm_daemon_config_get_xserver_list
 *
 * Returns the list of xservers being used.
 */
GSList *
gdm_daemon_config_get_xserver_list (GdmDaemonConfig *config)
{
	return config->priv->xservers;
}


static void
gdm_daemon_config_load_xserver (GdmDaemonConfig *config,
				const char      *key,
				const char      *name)
{
	GdmXserver     *svr;
	int             n;
	char           *group;
	gboolean        res;
	GdmConfigValue *value;

	/*
	 * See if we already loaded a server with this id, skip if
	 * one already exists.
	 */
	if (gdm_daemon_config_find_xserver (config, name) != NULL) {
		return;
	}

	svr = g_new0 (GdmXserver, 1);
	svr->id = g_strdup (name);

	if (isdigit (*key)) {
		svr->number = atoi (key);
	}

	group = g_strdup_printf ("server-%s", name);

	/* string */
	res = gdm_config_get_value (config->priv->config, group, "name", &value);
	if (res) {
		svr->name = g_strdup (gdm_config_value_get_string (value));
	}
	res = gdm_config_get_value (config->priv->config, group, "command", &value);
	if (res) {
		svr->command = g_strdup (gdm_config_value_get_string (value));
	}

	g_debug ("Adding new server id=%s name=%s command=%s", name, svr->name, svr->command);


	/* bool */
	res = gdm_config_get_value (config->priv->config, group, "flexible", &value);
	if (res) {
		svr->flexible = gdm_config_value_get_bool (value);
	}
	res = gdm_config_get_value (config->priv->config, group, "choosable", &value);
	if (res) {
		svr->choosable = gdm_config_value_get_bool (value);
	}
	res = gdm_config_get_value (config->priv->config, group, "handled", &value);
	if (res) {
		svr->handled = gdm_config_value_get_bool (value);
	}
	res = gdm_config_get_value (config->priv->config, group, "chooser", &value);
	if (res) {
		svr->chooser = gdm_config_value_get_bool (value);
	}

	/* int */
	res = gdm_config_get_value (config->priv->config, group, "priority", &value);
	if (res) {
		svr->priority = gdm_config_value_get_int (value);
	}

	/* do some bounds checking */
	n = svr->priority;
	if (n < PRIO_MIN) {
		n = PRIO_MIN;
	} else if (n > PRIO_MAX) {
		n = PRIO_MAX;
	}

	if (n != svr->priority) {
		g_warning (_("%s: Priority out of bounds; changed to %d"),
			   "gdm_config_parse", n);
		svr->priority = n;
	}

	if (svr->command == NULL || svr->command[0] == '\0') {
		g_warning (_("Empty server command; using standard command."));
		g_free (svr->command);
		svr->command = g_strdup (X_SERVER);
	}

	config->priv->xservers = g_slist_append (config->priv->xservers, svr);
}

static void
gdm_daemon_config_unload_xservers (GdmDaemonConfig *config)
{
	GSList *xli;

	/* Free list if already loaded */
	for (xli = config->priv->xservers; xli != NULL; xli = xli->next) {
		GdmXserver *xsvr = xli->data;

		g_free (xsvr->id);
		g_free (xsvr->name);
		g_free (xsvr->command);
	}

	if (config->priv->xservers != NULL) {
		g_slist_free (config->priv->xservers);
		config->priv->xservers = NULL;
	}
}

static void
gdm_daemon_config_ensure_one_xserver (GdmDaemonConfig *config)
{
	/* If no "Standard" server was created, then add it */
	if (config->priv->xservers == NULL || gdm_daemon_config_find_xserver (config, GDM_STANDARD) == NULL) {
		GdmXserver *svr = g_new0 (GdmXserver, 1);

		svr->id        = g_strdup (GDM_STANDARD);
		svr->name      = g_strdup ("Standard server");
		svr->command   = g_strdup (X_SERVER);
		svr->flexible  = TRUE;
		svr->choosable = TRUE;
		svr->handled   = TRUE;
		svr->priority  = 0;

		config->priv->xservers = g_slist_append (config->priv->xservers, svr);
	}
}

static void
load_xservers_group (GdmDaemonConfig *config)
{
        char     **keys;
        gsize      len;
        int        i;

        keys = gdm_config_get_keys_for_group (config->priv->config, GDM_CONFIG_GROUP_SERVERS, &len, NULL);

        /* now construct entries for these groups */
        for (i = 0; i < len; i++) {
                GdmConfigEntry  entry;
                GdmConfigValue *value;
                char           *new_group;
                gboolean        res;
                int             j;
		const char     *name;

                entry.group = GDM_CONFIG_GROUP_SERVERS;
                entry.key = keys[i];
                entry.type = GDM_CONFIG_VALUE_STRING;
                entry.default_value = NULL;
                entry.id = GDM_CONFIG_INVALID_ID;

                gdm_config_add_entry (config->priv->config, &entry);
                gdm_config_process_entry (config->priv->config, &entry, NULL);

                res = gdm_config_get_value (config->priv->config, entry.group, entry.key, &value);
                if (! res) {
                        continue;
                }

		name = gdm_config_value_get_string (value);
		if (name == NULL || name[0] == '\0') {
			gdm_config_value_free (value);
			continue;
		}

		/* skip servers marked as inactive */
		if (g_ascii_strcasecmp (name, "inactive") == 0) {
			gdm_config_value_free (value);
			continue;
		}

                new_group = g_strdup_printf ("server-%s", name);
                for (j = 0; j < G_N_ELEMENTS (gdm_daemon_server_config_entries); j++) {
                        GdmConfigEntry *srv_entry;
                        if (gdm_daemon_server_config_entries[j].key == NULL) {
                                continue;
                        }
                        srv_entry = gdm_config_entry_copy (&gdm_daemon_server_config_entries[j]);
                        g_free (srv_entry->group);
                        srv_entry->group = g_strdup (new_group);
                        gdm_config_process_entry (config->priv->config, srv_entry, NULL);
                        gdm_config_entry_free (srv_entry);
                }
		g_free (new_group);

		/* now we can add this server */
		gdm_daemon_config_load_xserver (config, entry.key, gdm_config_value_get_string (value));

                gdm_config_value_free (value);
        }
}

static void
gdm_daemon_config_load_xservers (GdmDaemonConfig *config)
{
	gdm_daemon_config_unload_xservers (config);
	load_xservers_group (config);
	gdm_daemon_config_ensure_one_xserver (config);
}

static void
load_config (GdmDaemonConfig *config)
{
	GError *error;

	config->priv->config = gdm_config_new ();

	gdm_config_set_notify_func (config->priv->config, notify_cb, NULL);
	gdm_config_set_validate_func (config->priv->config, validate_cb, NULL);

	gdm_config_add_static_entries (config->priv->config, gdm_daemon_config_entries);

	gdm_config_set_default_file (config->priv->config, config->priv->default_file);
	gdm_config_set_custom_file (config->priv->config, config->priv->custom_file);

	/* load the data files */
	error = NULL;
	gdm_config_load (config->priv->config, &error);
	if (error != NULL) {
		g_warning ("Unable to load configuration: %s", error->message);
		g_error_free (error);
	}

	/* populate the database with all specified entries */
	gdm_config_process_all (config->priv->config, &error);

	gdm_daemon_config_load_xservers (config);
}

void
gdm_daemon_config_load (GdmDaemonConfig *config)
{
	g_return_if_fail (GDM_IS_DAEMON_CONFIG (config));

	if (config->priv->config != NULL) {
		return;
	}

	load_config (config);
}

static void
gdm_daemon_config_init (GdmDaemonConfig *config)
{

	config->priv = GDM_DAEMON_CONFIG_GET_PRIVATE (config);

	config->priv->default_file = GDM_DEFAULTS_CONF;
	config->priv->custom_file = GDM_CUSTOM_CONF;
}

static void
gdm_daemon_config_finalize (GObject *object)
{
	GdmDaemonConfig *config;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_DAEMON_CONFIG (object));

	config = GDM_DAEMON_CONFIG (object);

	g_return_if_fail (config->priv != NULL);

	if (config->priv->xservers != NULL) {
		g_slist_free (config->priv->xservers);
	}

	G_OBJECT_CLASS (gdm_daemon_config_parent_class)->finalize (object);
}

GdmDaemonConfig *
gdm_daemon_config_new (void)
{
	if (daemon_config_object != NULL) {
		g_object_ref (daemon_config_object);
	} else {
		daemon_config_object = g_object_new (GDM_TYPE_DAEMON_CONFIG, NULL);
		g_object_add_weak_pointer (daemon_config_object,
					   (gpointer *) &daemon_config_object);
	}

	return GDM_DAEMON_CONFIG (daemon_config_object);
}
