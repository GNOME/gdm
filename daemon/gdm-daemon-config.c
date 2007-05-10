/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 * Copyright (C) 2005 Brian Cameron <brian.cameron@sun.com>
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

/*
 * gdm-daemon-config.c isolates most logic that interacts with GDM
 * configuration into a single file and provides a mechanism for
 * interacting with GDM configuration optins via access functions for
 * getting/setting values.  This logic also ensures that the same
 * configuration validation happens when loading the values initially
 * or setting them via the GDM_UPDATE_CONFIG socket command.
 *
 * When adding a new configuration option, simply add the new option
 * to gdm-daemon-config-entries.h.  Any validation for the
 * configuration option should be placed in the validate_cb function.
 */

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "gdm.h"
#include "verify.h"
#include "gdm-net.h"
#include "misc.h"
#include "server.h"
#include "filecheck.h"
#include "slave.h"

#include "gdm-common.h"
#include "gdm-config.h"
#include "gdm-log.h"
#include "gdm-daemon-config.h"

#include "gdm-socket-protocol.h"

static GdmConfig *daemon_config = NULL;

static GSList *displays = NULL;
static GSList *xservers = NULL;

static gint high_display_num = 0;
static char *custom_config_file = NULL;

static uid_t GdmUserId;   /* Userid  under which gdm should run */
static gid_t GdmGroupId;  /* Gruopid under which gdm should run */

/**
 * is_key
 *
 * Since GDM keys sometimes have default values defined in the gdm.h header
 * file (e.g. key=value), this function strips off the "=value" from both 
 * keys passed and compares them, returning TRUE if they are the same, 
 * FALSE otherwise.
 */
static gboolean
is_key (const gchar *key1, const gchar *key2)
{
	gchar *key1d, *key2d, *p;

	key1d = g_strdup (key1);
	key2d = g_strdup (key2);

	g_strstrip (key1d);
	p = strchr (key1d, '=');
	if (p != NULL)
		*p = '\0';

	g_strstrip (key2d);
	p = strchr (key2d, '=');
	if (p != NULL)
		*p = '\0';

	if (strcmp (ve_sure_string (key1d), ve_sure_string (key2d)) == 0) {
		g_free (key1d);
		g_free (key2d);
		return TRUE;
	} else {
		g_free (key1d);
		g_free (key2d);
		return FALSE;
	}
}

/**
 * gdm_daemon_config_get_per_display_custom_config_file
 *
 * Returns the per-display config file for a given display
 * This is always the custom config file name with the display
 * appended, and never gdm.conf.
 */
static gchar *
gdm_daemon_config_get_per_display_custom_config_file (const gchar *display)
{
	return g_strdup_printf ("%s%s", custom_config_file, display);
}

/**
 * gdm_daemon_config_get_custom_config_file
 *
 * Returns the custom config file being used.
 */
gchar *
gdm_daemon_config_get_custom_config_file (void)
{
	return custom_config_file;
}

/**
 * gdm_daemon_config_get_display_list
 *
 * Returns the list of displays being used.
 */
GSList *
gdm_daemon_config_get_display_list (void)
{
	return displays;
}

GSList *
gdm_daemon_config_display_list_append (GdmDisplay *display)
{
	displays = g_slist_append (displays, display);
	return displays;
}

GSList *
gdm_daemon_config_display_list_insert (GdmDisplay *display)
{
        displays = g_slist_insert_sorted (displays,
                                          display,
                                          gdm_daemon_config_compare_displays);
	return displays;
}

GSList *
gdm_daemon_config_display_list_remove (GdmDisplay *display)
{
	displays = g_slist_remove (displays, display);

	return displays;
}

/**
 * gdm_daemon_config_get_value_int
 *
 * Gets an integer configuration option by key.  The option must
 * first be loaded, say, by calling gdm_config_parse.
 */
gint
gdm_daemon_config_get_value_int (const char *keystring)
{
	gboolean res;
	GdmConfigValue *value;
	char *group;
	char *key;
	int   result;

	result = 0;

	res = gdm_common_config_parse_key_string (keystring,
						  &group,
						  &key,
						  NULL,
						  NULL);
	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		goto out;
	}

	res = gdm_config_get_value (daemon_config,
				    group,
				    key,
				    &value);
	if (! res) {
		gdm_error ("Request for invalid configuration key %s", keystring);
		goto out;
	}

	if (value->type != GDM_CONFIG_VALUE_INT) {
		gdm_error ("Request for configuration key %s, but not type INT", keystring);
		goto out;
	}

	result = gdm_config_value_get_int (value);
 out:
	g_free (group);
	g_free (key);

	return result;
}

/**
 * gdm_daemon_config_get_value_string
 *
 * Gets a string configuration option by key.  The option must
 * first be loaded, say, by calling gdm_daemon_config_parse.
 */
const char *
gdm_daemon_config_get_value_string (const char *keystring)
{
	gboolean res;
	GdmConfigValue *value;
	char *group;
	char *key;
	const char *result;

	result = NULL;

	res = gdm_common_config_parse_key_string (keystring,
						  &group,
						  &key,
						  NULL,
						  NULL);
	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		goto out;
	}

	res = gdm_config_get_value (daemon_config,
				    group,
				    key,
				    &value);
	if (! res) {
		gdm_error ("Request for invalid configuration key %s", keystring);
		goto out;
	}

	if (value->type != GDM_CONFIG_VALUE_STRING) {
		gdm_error ("Request for configuration key %s, but not type STRING", keystring);
		goto out;
	}

	result = gdm_config_value_get_string (value);
 out:
	g_free (group);
	g_free (key);

	return result;
}

/**
 * gdm_daemon_config_get_value_string_array
 *
 * Gets a string configuration option by key.  The option must
 * first be loaded, say, by calling gdm_daemon_config_parse.
 */
const char **
gdm_daemon_config_get_value_string_array (const char *keystring)
{
	gboolean res;
	GdmConfigValue *value;
	char *group;
	char *key;
	const char **result;

	result = NULL;

	res = gdm_common_config_parse_key_string (keystring,
						  &group,
						  &key,
						  NULL,
						  NULL);
	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		goto out;
	}

	res = gdm_config_get_value (daemon_config,
				    group,
				    key,
				    &value);
	if (! res) {
		gdm_error ("Request for invalid configuration key %s", keystring);
		goto out;
	}

	if (value->type != GDM_CONFIG_VALUE_STRING_ARRAY) {
		gdm_error ("Request for configuration key %s, but not type STRING-ARRAY", keystring);
		goto out;
	}

	result = gdm_config_value_get_string_array (value);
 out:
	g_free (group);
	g_free (key);

	return result;
}

/**
 * gdm_daemon_config_get_bool_for_id
 *
 * Gets a boolean configuration option by ID.  The option must
 * first be loaded, say, by calling gdm_daemon_config_parse.
 */
gboolean
gdm_daemon_config_get_bool_for_id (int id)
{
	gboolean val;

	val = FALSE;
	gdm_config_get_bool_for_id (daemon_config, id, &val);

	return val;
}

/**
 * gdm_daemon_config_get_int_for_id
 *
 * Gets a integer configuration option by ID.  The option must
 * first be loaded, say, by calling gdm_daemon_config_parse.
 */
int
gdm_daemon_config_get_int_for_id (int id)
{
	int val;

	val = -1;
	gdm_config_get_int_for_id (daemon_config, id, &val);

	return val;
}

/**
 * gdm_daemon_config_get_string_for_id
 *
 * Gets a string configuration option by ID.  The option must
 * first be loaded, say, by calling gdm_daemon_config_parse.
 */
const char *
gdm_daemon_config_get_string_for_id (int id)
{
	const char *val;

	val = NULL;
	gdm_config_peek_string_for_id (daemon_config, id, &val);

	return val;
}

/**
 * gdm_daemon_config_get_value_bool
 *
 * Gets a boolean configuration option by key.  The option must
 * first be loaded, say, by calling gdm_daemon_config_parse.
 */
gboolean
gdm_daemon_config_get_value_bool (const char *keystring)
{
	gboolean res;
	GdmConfigValue *value;
	char *group;
	char *key;
	gboolean result;

	result = FALSE;

	res = gdm_common_config_parse_key_string (keystring,
						  &group,
						  &key,
						  NULL,
						  NULL);
	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		goto out;
	}

	res = gdm_config_get_value (daemon_config,
				    group,
				    key,
				    &value);
	if (! res) {
		gdm_error ("Request for invalid configuration key %s", keystring);
		goto out;
	}

	if (value->type != GDM_CONFIG_VALUE_BOOL) {
		gdm_error ("Request for configuration key %s, but not type BOOLEAN", keystring);
		goto out;
	}

	result = gdm_config_value_get_bool (value);
 out:
	g_free (group);
	g_free (key);

	return result;
}

/**
 * Note that some GUI configuration parameters are read by the daemon,
 * and in order for them to work, it is necessary for the daemon to 
 * access a few keys in a per-display fashion.  These access functions
 * allow the daemon to access these keys properly.
 */

/**
 * gdm_daemon_config_get_value_int_per_display
 *
 * Gets the per-display version  of the configuration, or the default
 * value if none exists.
 */
int
gdm_daemon_config_get_value_int_per_display (const char *key,
					     const char *display)
{
	char    *perdispval;
	gboolean res;

	res = gdm_daemon_config_key_to_string_per_display (key, display, &perdispval);

	if (res) {
		int val;
		val = atoi (perdispval);
		g_free (perdispval);
		return val;
	} else {
		return gdm_daemon_config_get_value_int (key);
	}
}

/**
 * gdm_daemon_config_get_value_bool_per_display
 *
 * Gets the per-display version  of the configuration, or the default
 * value if none exists.
 */
gboolean
gdm_daemon_config_get_value_bool_per_display (const char *key,
					      const char *display)
{
	char    *perdispval;
	gboolean res;

	res = gdm_daemon_config_key_to_string_per_display (key, display, &perdispval);

	if (res) {
		if (perdispval[0] == 'T' ||
		    perdispval[0] == 't' ||
		    perdispval[0] == 'Y' ||
		    perdispval[0] == 'y' ||
		    atoi (perdispval) != 0) {
			g_free (perdispval);
			return TRUE;
		} else {
			return FALSE;
		}
	} else {
		return gdm_daemon_config_get_value_bool (key);
	}
}

/**
 * gdm_daemon_config_get_value_string_per_display
 *
 * Gets the per-display version  of the configuration, or the default
 * value if none exists.  Note that this value needs to be freed,
 * unlike the non-per-display version.
 */
char *
gdm_daemon_config_get_value_string_per_display (const char *key,
						const char *display)
{
	char    *perdispval;
	gboolean res;

	res = gdm_daemon_config_key_to_string_per_display (key, display, &perdispval);

	if (res) {
		return perdispval;
	} else {
		return g_strdup (gdm_daemon_config_get_value_string (key));
	}
}

/**
 * gdm_daemon_config_key_to_string_per_display
 *
 * If the key makes sense to be per-display, return the value,
 * otherwise return NULL.  Keys that only apply to the daemon
 * process do not make sense for per-display configuration
 * Valid keys include any key in the greeter or gui categories,
 * and the GDM_KEY_PAM_STACK key.
 *
 * If additional keys make sense for per-display usage, make
 * sure they are added to the if-test below.
 */
gboolean
gdm_daemon_config_key_to_string_per_display (const char *keystring,
					     const char *display,
					     char      **retval)
{
	char    *file;
	char    *group;
	char    *key;
	gboolean res;
	gboolean ret;

	ret = FALSE;

	*retval = NULL;
	group = key = NULL;

	if (display == NULL) {
		goto out;
	}

	g_debug ("Looking up per display value for %s", keystring);

	res = gdm_common_config_parse_key_string (keystring,
						  &group,
						  &key,
						  NULL,
						  NULL);
	if (! res) {
		goto out;
	}

	file = gdm_daemon_config_get_per_display_custom_config_file (display);

	if (strcmp (group, "greeter") == 0 ||
	    strcmp (group, "gui") == 0 ||
	    is_key (keystring, GDM_KEY_PAM_STACK)) {
		ret = gdm_daemon_config_key_to_string (file, keystring, retval);
	}

	g_free (file);

 out:
	g_free (group);
	g_free (key);

	return ret;
}

/**
 * gdm_daemon_config_key_to_string
 *
 * Gets a specific key from the config file.
 * Note this returns the value in string form, so the caller needs
 * to parse it properly if it is a bool or int.
 *
 * Returns TRUE if successful..
 */
gboolean
gdm_daemon_config_key_to_string (const char *file,
				 const char *keystring,
				 char      **retval)
{
	GKeyFile             *config;
	GdmConfigValueType    type;
	gboolean              res;
	gboolean              ret;
	char                 *group;
	char                 *key;
	char                 *locale;
	char                 *result;
	const GdmConfigEntry *entry;

	if (retval != NULL) {
		*retval = NULL;
	}

	ret = FALSE;
	result = NULL;

	group = key = locale = NULL;
	res = gdm_common_config_parse_key_string (keystring,
						  &group,
						  &key,
						  &locale,
						  NULL);
	g_debug ("Requesting group=%s key=%s locale=%s", group, key, locale ? locale : "(null)");

	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		goto out;
	}

	entry = gdm_config_lookup_entry (daemon_config, group, key);
	if (entry == NULL) {
		gdm_error ("Request for invalid configuration key %s", keystring);
		goto out;
	}
	type = entry->type;

	config = gdm_common_config_load (file, NULL);
	/* If file doesn't exist, then just return */
	if (config == NULL) {
		goto out;
	}

	gdm_debug ("Returning value for key <%s>\n", keystring);

	switch (type) {
	case GDM_CONFIG_VALUE_BOOL:
		{
			gboolean value;
			res = gdm_common_config_get_boolean (config, keystring, &value, NULL);
			if (res) {
				if (value) {
					result = g_strdup ("true");
				} else {
					result = g_strdup ("false");
				}
			}
		}
		break;
	case GDM_CONFIG_VALUE_INT:
		{
			int value;
			res = gdm_common_config_get_int (config, keystring, &value, NULL);
			if (res) {
				result = g_strdup_printf ("%d", value);
			}
		}
		break;
	case GDM_CONFIG_VALUE_STRING:
		{
			char *value;
			res = gdm_common_config_get_string (config, keystring, &value, NULL);
			if (res) {
				result = value;
			}
		}
		break;
	case GDM_CONFIG_VALUE_LOCALE_STRING:
		{
			char *value;
			res = gdm_common_config_get_string (config, keystring, &value, NULL);
			if (res) {
				result = value;
			}
		}
		break;
	default:
		break;
	}

	if (res) {
		if (retval != NULL) {
			*retval = g_strdup (result);
		}
		ret = TRUE;
	}

	g_key_file_free (config);
 out:
	g_free (result);
	g_free (group);
	g_free (key);
	g_free (locale);

	return ret;
}

/**
 * gdm_daemon_config_to_string
 *
 * Returns a configuration option as a string.  Used by GDM's
 * GET_CONFIG socket command.
 */
gboolean
gdm_daemon_config_to_string (const char *keystring,
			     const char *display,
			     char      **retval)
{
	gboolean res;
	gboolean ret;
	GdmConfigValue *value;
	char *group;
	char *key;
	char *locale;
	char *result;

	/*
	 * See if there is a per-display config file, returning that value
	 * if it exists.
	 */
	if (display != NULL) {
		res = gdm_daemon_config_key_to_string_per_display (keystring, display, retval);
		if (res) {
			g_debug ("Using per display value for key: %s", keystring);
			return TRUE;
		}
	}

	ret = FALSE;
	result = NULL;

	g_debug ("Looking up key: %s", keystring);

	group = NULL;
	key = NULL;
	locale = NULL;
	res = gdm_common_config_parse_key_string (keystring,
						  &group,
						  &key,
						  &locale,
						  NULL);
	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		goto out;
	}

	if (group == NULL || key == NULL) {
		gdm_error ("Request for invalid configuration key %s", keystring);
		goto out;
	}

	/* Backward Compatibility */
	if ((strcmp (group, "daemon") == 0) &&
	    (strcmp (key, "PidFile") == 0)) {
		result = g_strdup (GDM_PID_FILE);
		goto out;
	} else if ((strcmp (group, "daemon") == 0) &&
		   (strcmp (key, "AlwaysRestartServer") == 0)) {
		result = g_strdup ("true");
	}

	res = gdm_config_get_value (daemon_config,
				    group,
				    key,
				    &value);

	if (! res) {
		gdm_error ("Request for invalid configuration key %s", keystring);
		goto out;
	}

	result = gdm_config_value_to_string (value);
	ret = TRUE;

 out:
	g_free (group);
	g_free (key);
	g_free (locale);


	*retval = result;

	return ret;
}

/**
 * gdm_daemon_config_compare_displays
 *
 * Support function for loading displays from the configuration
 * file
 */
int
gdm_daemon_config_compare_displays (gconstpointer a, gconstpointer b)
{
	const GdmDisplay *d1 = a;
	const GdmDisplay *d2 = b;
	if (d1->dispnum < d2->dispnum)
		return -1;
	else if (d1->dispnum > d2->dispnum)
		return 1;
	else
		return 0;
}

static char *
lookup_notify_key (GdmConfig  *config,
		   const char *group,
		   const char *key)
{
	char *nkey;
	char *keystring;

	keystring = g_strdup_printf ("%s/%s", group, key);

	/* pretty lame but oh well */
	nkey = NULL;

	/* bools */
	if (is_key (keystring, GDM_KEY_ALLOW_ROOT))
		nkey = g_strdup (GDM_NOTIFY_ALLOW_ROOT);
	else if (is_key (keystring, GDM_KEY_ALLOW_REMOTE_ROOT))
		nkey = g_strdup (GDM_NOTIFY_ALLOW_REMOTE_ROOT);
	else if (is_key (keystring, GDM_KEY_ALLOW_REMOTE_AUTOLOGIN))
		nkey = g_strdup (GDM_NOTIFY_ALLOW_REMOTE_AUTOLOGIN);
	else if (is_key (keystring, GDM_KEY_SYSTEM_MENU))
		nkey = g_strdup (GDM_NOTIFY_SYSTEM_MENU);
	else if (is_key (keystring, GDM_KEY_CONFIG_AVAILABLE))
		nkey = g_strdup (GDM_NOTIFY_CONFIG_AVAILABLE);
	else if (is_key (keystring, GDM_KEY_CHOOSER_BUTTON))
		nkey = g_strdup (GDM_NOTIFY_CHOOSER_BUTTON);
	else if (is_key (keystring, GDM_KEY_DISALLOW_TCP))
		nkey = g_strdup (GDM_NOTIFY_DISALLOW_TCP);
	else if (is_key (keystring, GDM_KEY_ADD_GTK_MODULES))
		nkey = g_strdup (GDM_NOTIFY_ADD_GTK_MODULES);
	else if (is_key (keystring, GDM_KEY_TIMED_LOGIN_ENABLE))
		nkey = g_strdup (GDM_NOTIFY_TIMED_LOGIN_ENABLE);
	/* ints */
	else if (is_key (keystring, GDM_KEY_RETRY_DELAY))
		nkey = g_strdup (GDM_NOTIFY_RETRY_DELAY);
	else if (is_key (keystring, GDM_KEY_TIMED_LOGIN_DELAY))
		nkey = g_strdup (GDM_NOTIFY_TIMED_LOGIN_DELAY);
	/* strings */
	else if (is_key (keystring, GDM_KEY_GREETER))
		nkey = g_strdup (GDM_NOTIFY_GREETER);
	else if (is_key (keystring, GDM_KEY_REMOTE_GREETER))
		nkey = g_strdup (GDM_NOTIFY_REMOTE_GREETER);
	else if (is_key (keystring, GDM_KEY_SOUND_ON_LOGIN_FILE))
		nkey = g_strdup (GDM_NOTIFY_SOUND_ON_LOGIN_FILE);
	else if (is_key (keystring, GDM_KEY_SOUND_ON_LOGIN_SUCCESS_FILE))
		nkey = g_strdup (GDM_NOTIFY_SOUND_ON_LOGIN_SUCCESS_FILE);
	else if (is_key (keystring, GDM_KEY_SOUND_ON_LOGIN_FAILURE_FILE))
		nkey = g_strdup (GDM_NOTIFY_SOUND_ON_LOGIN_FAILURE_FILE);
	else if (is_key (keystring, GDM_KEY_GTK_MODULES_LIST))
		nkey = g_strdup (GDM_NOTIFY_GTK_MODULES_LIST);
	else if (is_key (keystring, GDM_KEY_TIMED_LOGIN))
		nkey = g_strdup (GDM_NOTIFY_TIMED_LOGIN);
	else if (strcmp (group, GDM_CONFIG_GROUP_CUSTOM_CMD) == 0 &&
		 g_str_has_prefix (key, "CustomCommand") &&
		 strlen (key) == 14) {
		/* this should match 'CustomCommandN' */
		nkey = g_strdup (key);
	}
	g_free (keystring);

	return nkey;
}

/**
 * notify_displays_value
 *
 * This will notify the slave programs
 * (gdmgreeter, gdmlogin, etc.) that a configuration option has
 * been changed so the slave can update with the new option
 * value.  GDM does this notify when it receives a
 * GDM_CONFIG_UPDATE socket command from gdmsetup or from the
 * gdmflexiserver --command option.
 */
static void
notify_displays_value (GdmConfig      *config,
		       const char     *group,
		       const char     *key,
		       GdmConfigValue *value)
{
	GSList *li;
	char   *valstr;
	char   *keystr;

	keystr = lookup_notify_key (config, group, key);

	/* unfortunately, can't always gdm_config_value_to_string()
	 * here because booleans need to be sent as ints
	 */
	switch (value->type) {
	case GDM_CONFIG_VALUE_BOOL:
		if (gdm_config_value_get_bool (value)) {
			valstr = g_strdup ("1");
		} else {
			valstr = g_strdup ("0");
		}
		break;
	default:
		valstr = gdm_config_value_to_string (value);
		break;
	}

	if (valstr == NULL) {
		valstr = g_strdup (" ");
	}

	for (li = displays; li != NULL; li = li->next) {
		GdmDisplay *disp = li->data;

		if (disp->master_notify_fd < 0) {
			/* no point */
			continue;
		}

		gdm_fdprintf (disp->master_notify_fd,
			      "%c%s %s\n",
			      GDM_SLAVE_NOTIFY_KEY,
			      keystr,
			      valstr);

		if (disp != NULL && disp->slavepid > 1) {
			kill (disp->slavepid, SIGUSR2);
		}
	}

	g_free (keystr);
	g_free (valstr);
}

/* The following were used to internally set the
 * stored configuration values.  Now we'll just
 * ask the GdmConfig to store the entry. */
void
gdm_daemon_config_set_value_string (const gchar *keystring,
				    const gchar *value_in)
{
	char           *group;
	char           *key;
	gboolean        res;
	GdmConfigValue *value;

	res = gdm_common_config_parse_key_string (keystring, &group, &key, NULL, NULL);
	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		return;
	}

	value = gdm_config_value_new (GDM_CONFIG_VALUE_STRING);
	gdm_config_value_set_string (value, value_in);

	res = gdm_config_set_value (daemon_config, group, key, value);

	gdm_config_value_free (value);
	g_free (group);
	g_free (key);
}

void
gdm_daemon_config_set_value_bool (const gchar *keystring,
				  gboolean     value_in)
{
	char           *group;
	char           *key;
	gboolean        res;
	GdmConfigValue *value;

	res = gdm_common_config_parse_key_string (keystring, &group, &key, NULL, NULL);
	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		return;
	}

	value = gdm_config_value_new (GDM_CONFIG_VALUE_BOOL);
	gdm_config_value_set_bool (value, value_in);

	res = gdm_config_set_value (daemon_config, group, key, value);

	gdm_config_value_free (value);
	g_free (group);
	g_free (key);
}

void
gdm_daemon_config_set_value_int (const gchar *keystring,
				 gint         value_in)
{
	char           *group;
	char           *key;
	gboolean        res;
	GdmConfigValue *value;

	res = gdm_common_config_parse_key_string (keystring, &group, &key, NULL, NULL);
	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		return;
	}

	value = gdm_config_value_new (GDM_CONFIG_VALUE_INT);
	gdm_config_value_set_int (value, value_in);

	res = gdm_config_set_value (daemon_config, group, key, value);

	gdm_config_value_free (value);
	g_free (group);
	g_free (key);
}

/**
 * gdm_daemon_config_find_xserver
 *
 * Return an xserver with a given ID, or NULL if not found.
 */
GdmXserver *
gdm_daemon_config_find_xserver (const gchar *id)
{
	GSList *li;

	if (xservers == NULL)
		return NULL;

	if (id == NULL)
		return xservers->data;

	for (li = xservers; li != NULL; li = li->next) {
		GdmXserver *svr = li->data;
		if (strcmp (ve_sure_string (svr->id), ve_sure_string (id)) == 0)
			return svr;
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
gdm_daemon_config_get_xservers (void)
{
	GSList *li;
	gchar *retval = NULL;

	if (xservers == NULL)
		return NULL;

	for (li = xservers; li != NULL; li = li->next) {
		GdmXserver *svr = li->data;
		if (retval != NULL)
			retval = g_strconcat (retval, ";", svr->id, NULL);
		else
			retval = g_strdup (svr->id);
	}

	return retval;
}

/* PRIO_MIN and PRIO_MAX are not defined on Solaris, but are -20 and 20 */
#if sun
#ifndef PRIO_MIN
#define PRIO_MIN -20
#endif
#ifndef PRIO_MAX
#define PRIO_MAX 20
#endif
#endif

/**
 * gdm_daemon_config_load_xserver
 *
 * Load [server-foo] sections from a configuration file.
 */
static void
gdm_daemon_config_load_xserver (GdmConfig  *config,
				const char *key,
				const char *name)
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
	if (gdm_daemon_config_find_xserver (name) != NULL) {
		return;
	}

	svr = g_new0 (GdmXserver, 1);
	svr->id = g_strdup (name);

	if (isdigit (*key)) {
		svr->number = atoi (key);
	}

	group = g_strdup_printf ("server-%s", name);

	/* string */
	res = gdm_config_get_value (config, group, "name", &value);
	if (res) {
		svr->name = g_strdup (gdm_config_value_get_string (value));
	}
	res = gdm_config_get_value (config, group, "command", &value);
	if (res) {
		svr->command = g_strdup (gdm_config_value_get_string (value));
	}

	gdm_debug ("Adding new server id=%s name=%s command=%s", name, svr->name, svr->command);


	/* bool */
	res = gdm_config_get_value (config, group, "flexible", &value);
	if (res) {
		svr->flexible = gdm_config_value_get_bool (value);
	}
	res = gdm_config_get_value (config, group, "choosable", &value);
	if (res) {
		svr->choosable = gdm_config_value_get_bool (value);
	}
	res = gdm_config_get_value (config, group, "handled", &value);
	if (res) {
		svr->handled = gdm_config_value_get_bool (value);
	}
	res = gdm_config_get_value (config, group, "chooser", &value);
	if (res) {
		svr->chooser = gdm_config_value_get_bool (value);
	}

	/* int */
	res = gdm_config_get_value (config, group, "priority", &value);
	if (res) {
		svr->priority = gdm_config_value_get_int (value);
	}

	/* do some bounds checking */
	n = svr->priority;
	if (n < PRIO_MIN)
		n = PRIO_MIN;
	else if (n > PRIO_MAX)
		n = PRIO_MAX;

	if (n != svr->priority) {
		gdm_error (_("%s: Priority out of bounds; changed to %d"),
			   "gdm_config_parse", n);
		svr->priority = n;
	}

	if (ve_string_empty (svr->command)) {
		gdm_error (_("%s: Empty server command; "
			     "using standard command."), "gdm_config_parse");
		g_free (svr->command);
		svr->command = g_strdup (X_SERVER);
	}

	xservers = g_slist_append (xservers, svr);
}

static void
gdm_daemon_config_unload_xservers (GdmConfig *config)
{
	GSList *xli;

	/* Free list if already loaded */
	for (xli = xservers; xli != NULL; xli = xli->next) {
		GdmXserver *xsvr = xli->data;

		g_free (xsvr->id);
		g_free (xsvr->name);
		g_free (xsvr->command);
	}

	if (xservers != NULL) {
		g_slist_free (xservers);
		xservers = NULL;
	}
}

static void
gdm_daemon_config_ensure_one_xserver (GdmConfig *config)
{
	/* If no "Standard" server was created, then add it */
	if (xservers == NULL || gdm_daemon_config_find_xserver (GDM_STANDARD) == NULL) {
		GdmXserver *svr = g_new0 (GdmXserver, 1);

		svr->id        = g_strdup (GDM_STANDARD);
		svr->name      = g_strdup ("Standard server");
		svr->command   = g_strdup (X_SERVER);
		svr->flexible  = TRUE;
		svr->choosable = TRUE;
		svr->handled   = TRUE;
		svr->priority  = 0;

		xservers       = g_slist_append (xservers, svr);
	}
}

static void
load_xservers_group (GdmConfig *config)
{
        char     **keys;
        gsize      len;
        int        i;

        keys = gdm_config_get_keys_for_group (config, GDM_CONFIG_GROUP_SERVERS, &len, NULL);

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

                gdm_config_add_entry (config, &entry);
                gdm_config_process_entry (config, &entry, NULL);

                res = gdm_config_get_value (config, entry.group, entry.key, &value);
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
                        gdm_config_process_entry (config, srv_entry, NULL);
                        gdm_config_entry_free (srv_entry);
                }
		g_free (new_group);

		/* now we can add this server */
		gdm_daemon_config_load_xserver (config, entry.key, gdm_config_value_get_string (value));

                gdm_config_value_free (value);
        }
}

static void
gdm_daemon_config_load_xservers (GdmConfig *config)
{
	gdm_daemon_config_unload_xservers (config);
	load_xservers_group (config);
	gdm_daemon_config_ensure_one_xserver (config);
}

/**
 * gdm_daemon_config_update_key
 *
 * Will cause a the GDM daemon to re-read the key from the configuration
 * file and cause notify signal to be sent to the slaves for the
 * specified key, if appropriate.
 * Obviously notification is not needed for configuration options only
 * used by the daemon.  This function is called when the UPDDATE_CONFIG
 * sockets command is called.
 *
 * To add a new notification, a GDM_NOTIFY_* argument will need to be
 * defined in gdm-daemon-config-keys.h, supporting logic placed in the
 * notify_cb function and in the gdm_slave_handle_notify function
 * in slave.c.
 */
gboolean
gdm_daemon_config_update_key (const char *keystring)
{
	gboolean              rc;
	gboolean              res;
	char                 *group;
	char                 *key;
	char                 *locale;
	const GdmConfigEntry *entry;

	rc = FALSE;
	group = key = locale = NULL;

	/*
	 * Do not allow these keys to be updated, since GDM would need
	 * additional work, or at least heavy testing, to make these keys
	 * flexible enough to be changed at runtime.
	 */
	if (is_key (keystring, GDM_KEY_PID_FILE) ||
	    is_key (keystring, GDM_KEY_CONSOLE_NOTIFY) ||
	    is_key (keystring, GDM_KEY_USER) ||
	    is_key (keystring, GDM_KEY_GROUP) ||
	    is_key (keystring, GDM_KEY_LOG_DIR) ||
	    is_key (keystring, GDM_KEY_SERV_AUTHDIR) ||
	    is_key (keystring, GDM_KEY_USER_AUTHDIR) ||
	    is_key (keystring, GDM_KEY_USER_AUTHFILE) ||
	    is_key (keystring, GDM_KEY_USER_AUTHDIR_FALLBACK)) {
		return FALSE;
	}

	/* reload backend files if necessary */
	gdm_config_load (daemon_config, NULL);

	/* Shortcut for updating all XDMCP parameters */
	if (is_key (keystring, "xdmcp/PARAMETERS")) {
		rc = gdm_daemon_config_update_key (GDM_KEY_DISPLAYS_PER_HOST);
                if (rc == TRUE)
			rc = gdm_daemon_config_update_key (GDM_KEY_MAX_PENDING);
                if (rc == TRUE)
			rc = gdm_daemon_config_update_key (GDM_KEY_MAX_WAIT);
                if (rc == TRUE)
			rc = gdm_daemon_config_update_key (GDM_KEY_MAX_SESSIONS);
                if (rc == TRUE)
			rc = gdm_daemon_config_update_key (GDM_KEY_INDIRECT);
                if (rc == TRUE)
			rc = gdm_daemon_config_update_key (GDM_KEY_MAX_INDIRECT);
                if (rc == TRUE)
			rc = gdm_daemon_config_update_key (GDM_KEY_MAX_WAIT_INDIRECT);
                if (rc == TRUE)
			rc = gdm_daemon_config_update_key (GDM_KEY_PING_INTERVAL);
		goto out;
	}

	/* find the entry for the key */
	res = gdm_common_config_parse_key_string (keystring,
						  &group,
						  &key,
						  &locale,
						  NULL);
	if (! res) {
		gdm_error ("Could not parse configuration key %s", keystring);
		goto out;
	}

	entry = gdm_config_lookup_entry (daemon_config, group, key);
	if (entry == NULL) {
		gdm_error ("Request for invalid configuration key %s", keystring);
		goto out;
	}

	rc = gdm_config_process_entry (daemon_config, entry, NULL);

 out:
	g_free (group);
	g_free (key);
	g_free (locale);

	return rc;
}

/**
 * check_logdir
 * check_servauthdir
 *
 * Support functions for gdm_config_parse.
 */
static void
check_logdir (void)
{
        struct stat     statbuf;
        int             r;
	char           *log_path;
	const char     *auth_path;
	GdmConfigValue *value;

	log_path = NULL;
	auth_path = NULL;

	gdm_config_get_string_for_id (daemon_config, GDM_ID_LOG_DIR, &log_path);

	gdm_config_get_value_for_id (daemon_config, GDM_ID_SERV_AUTHDIR, &value);
	auth_path = gdm_config_value_get_string (value);

        VE_IGNORE_EINTR (r = g_stat (log_path, &statbuf));
        if (r < 0 || ! S_ISDIR (statbuf.st_mode))  {
                gdm_error (_("%s: Logdir %s does not exist or isn't a directory.  Using ServAuthDir %s."),
			   "gdm_config_parse",
                           log_path,
			   auth_path);
		gdm_config_set_value_for_id (daemon_config, GDM_ID_LOG_DIR, value);
        }

	g_free (log_path);
	gdm_config_value_free (value);
}

static void
check_servauthdir (const char  *auth_path,
		   struct stat *statbuf)
{
	int        r;
	gboolean   console_notify;

	console_notify = FALSE;
	gdm_config_get_bool_for_id (daemon_config, GDM_ID_CONSOLE_NOTIFY, &console_notify);

	/* Enter paranoia mode */
	VE_IGNORE_EINTR (r = g_stat (auth_path, statbuf));
	if G_UNLIKELY (r < 0) {
		if (console_notify) {
			gchar *s = g_strdup_printf (C_(N_("Server Authorization directory "
							  "(daemon/ServAuthDir) is set to %s "
							  "but this does not exist. Please "
							  "correct GDM configuration and "
							  "restart GDM.")),
						    auth_path);

			gdm_text_message_dialog (s);
			g_free (s);
		}

		gdm_fail (_("%s: Authdir %s does not exist. Aborting."), "gdm_config_parse", auth_path);
	}

	if G_UNLIKELY (! S_ISDIR (statbuf->st_mode)) {
		if (console_notify) {
			gchar *s = g_strdup_printf (C_(N_("Server Authorization directory "
							  "(daemon/ServAuthDir) is set to %s "
							  "but this is not a directory. Please "
							  "correct GDM configuration and "
							  "restart GDM.")),
						    auth_path);

			gdm_text_message_dialog (s);
			g_free (s);
		}

		gdm_fail (_("%s: Authdir %s is not a directory. Aborting."), "gdm_config_parse", auth_path);
	}
}

static gboolean
have_display_for_number (int number)
{
	GSList *l;

	for (l = displays; l != NULL; l = l->next) {
		GdmDisplay *disp = l->data;
		if (disp->dispnum == number) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
gdm_daemon_config_load_displays (GdmConfig *config)
{
	GSList *l;

	for (l = xservers; l != NULL; l = l->next) {
		GdmXserver *xserver;
		GdmDisplay *disp;

		xserver = l->data;

		gdm_debug ("Loading display for key '%d'", xserver->number);

		gdm_debug ("Got name for server: %s", xserver->id);
		/* Do not add if already in the list */
		if (have_display_for_number (xserver->number)) {
			continue;
		}

		disp = gdm_display_alloc (xserver->number, xserver->id);
		if (disp == NULL) {
			continue;
		}

		displays = g_slist_insert_sorted (displays, disp, gdm_daemon_config_compare_displays);
		if (xserver->number > high_display_num) {
			high_display_num = xserver->number;
		}
	}
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
		gdm_info (_("%s: BaseXsession empty; using %s"), "gdm_config_parse", path);
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
		gdm_info (_("%s: Standard X server not found; trying alternatives"), "gdm_config_parse");
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
		gdm_error (_("%s: No greeter specified."), "gdm_config_parse");
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
		gdm_error (_("%s: No remote greeter specified."), "gdm_config_parse");
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
		gdm_error (_("%s: No sessions directory specified."), "gdm_config_parse");
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
		gdm_info (_("%s: XDMCP was enabled while there is no XDMCP support; turning it off"), "gdm_config_parse");
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

	valstr = gdm_config_value_to_string (value);
	gdm_debug ("Got config %s/%s=%s <%s>\n",
		   group,
		   key,
		   valstr,
		   source_to_name (source));
	g_free (valstr);

        return TRUE;
}

static void
handle_no_displays (GdmConfig *config,
		    gboolean   no_console)
{
	const char *server;
	gboolean    console_notify;

	console_notify = FALSE;
	gdm_config_get_bool_for_id (daemon_config, GDM_ID_CONSOLE_NOTIFY, &console_notify);

	/*
	 * If we requested no static servers (there is no console),
	 * then don't display errors in console messages
	 */
	if (no_console) {
		gdm_fail (_("%s: XDMCP disabled and no static servers defined. Aborting!"), "gdm_config_parse");
	}

	server = X_SERVER;
	if G_LIKELY (g_access (server, X_OK) == 0) {
	} else if (g_access ("/usr/bin/X11/X", X_OK) == 0) {
		server = "/usr/bin/X11/X";
	} else if (g_access ("/usr/X11R6/bin/X", X_OK) == 0) {
		server = "/usr/X11R6/bin/X";
	} else if (g_access ("/opt/X11R6/bin/X", X_OK) == 0) {
		server = "/opt/X11R6/bin/X";
	}

	/* yay, we can add a backup emergency server */
	if (server != NULL) {
		GdmDisplay *d;

		int num = gdm_get_free_display (0 /* start */, 0 /* server uid */);

		gdm_error (_("%s: XDMCP disabled and no static servers defined. Adding %s on :%d to allow configuration!"),
			   "gdm_config_parse", server, num);

		d = gdm_display_alloc (num, server);
		d->is_emergency_server = TRUE;

		displays = g_slist_append (displays, d);

		/* ALWAYS run the greeter and don't log anyone in,
		 * this is just an emergency session */
		gdm_config_set_string_for_id (daemon_config, GDM_ID_AUTOMATIC_LOGIN, NULL);
		gdm_config_set_string_for_id (daemon_config, GDM_ID_TIMED_LOGIN, NULL);

	} else {
		if (console_notify) {
			gchar *s = g_strdup_printf (C_(N_("XDMCP is disabled and GDM "
							  "cannot find any static server "
							  "to start.  Aborting!  Please "
							  "correct the configuration "
							  "and restart GDM.")));
			gdm_text_message_dialog (s);
			g_free (s);
		}

		gdm_fail (_("%s: XDMCP disabled and no static servers defined. Aborting!"), "gdm_config_parse");
	}
}

static void
gdm_daemon_change_user (GdmConfig *config,
			uid_t     *uidp,
			gid_t     *gidp)
{
	gboolean    console_notify;
	char       *username;
	char       *groupname;
	uid_t       uid;
	gid_t       gid;
	struct passwd *pwent;
	struct group  *grent;

	console_notify = FALSE;
	username = NULL;
	groupname = NULL;
	uid = 0;
	gid = 0;

	gdm_config_get_bool_for_id (daemon_config, GDM_ID_CONSOLE_NOTIFY, &console_notify);
	gdm_config_get_string_for_id (daemon_config, GDM_ID_USER, &username);
	gdm_config_get_string_for_id (daemon_config, GDM_ID_GROUP, &groupname);

	/* Lookup user and groupid for the GDM user */
	pwent = getpwnam (username);

	/* Set uid and gid */
	if G_UNLIKELY (pwent == NULL) {

		if (console_notify) {
			gchar *s = g_strdup_printf (C_(N_("The GDM user '%s' does not exist. "
							  "Please correct GDM configuration "
							  "and restart GDM.")),
						    username);
			gdm_text_message_dialog (s);
			g_free (s);
		}

		gdm_fail (_("%s: Can't find the GDM user '%s'. Aborting!"), "gdm_config_parse", username);
	} else {
		uid = pwent->pw_uid;
	}

	if G_UNLIKELY (uid == 0) {
		if (console_notify) {
			gchar *s = g_strdup_printf (C_(N_("The GDM user is set to be root, but "
							  "this is not allowed since it can "
							  "pose a security risk.  Please "
							  "correct GDM configuration and "
							  "restart GDM.")));

			gdm_text_message_dialog (s);
			g_free (s);
		}

		gdm_fail (_("%s: The GDM user should not be root. Aborting!"), "gdm_config_parse");
	}

	grent = getgrnam (groupname);

	if G_UNLIKELY (grent == NULL) {
		if (console_notify) {
			gchar *s = g_strdup_printf (C_(N_("The GDM group '%s' does not exist. "
							  "Please correct GDM configuration "
							  "and restart GDM.")),
						    groupname);
			gdm_text_message_dialog (s);
			g_free (s);
		}

		gdm_fail (_("%s: Can't find the GDM group '%s'. Aborting!"), "gdm_config_parse", groupname);
	} else  {
		gid = grent->gr_gid;
	}

	if G_UNLIKELY (gid == 0) {
		if (console_notify) {
			gchar *s = g_strdup_printf (C_(N_("The GDM group is set to be root, but "
							  "this is not allowed since it can "
							  "pose a security risk. Please "
							  "correct GDM configuration and "
							  "restart GDM.")));
			gdm_text_message_dialog (s);
			g_free (s);
		}

		gdm_fail (_("%s: The GDM group should not be root. Aborting!"), "gdm_config_parse");
	}

	/* gid remains `gdm' */
	NEVER_FAILS_root_set_euid_egid (uid, gid);

	if (uidp != NULL) {
		*uidp = uid;
	}

	if (gidp != NULL) {
		*gidp = gid;
	}

	g_free (username);
	g_free (groupname);
}

static void
gdm_daemon_check_permissions (GdmConfig *config,
			      uid_t      uid,
			      gid_t      gid)
{
	struct stat statbuf;
	char       *auth_path;
	gboolean    console_notify;

	console_notify = FALSE;
	gdm_config_get_bool_for_id (daemon_config, GDM_ID_CONSOLE_NOTIFY, &console_notify);
	auth_path = NULL;
	gdm_config_get_string_for_id (config, GDM_ID_LOG_DIR, &auth_path);

	/* Enter paranoia mode */
	check_servauthdir (auth_path, &statbuf);

	NEVER_FAILS_root_set_euid_egid (0, 0);

	/* Now set things up for us as  */
	chown (auth_path, 0, gid);
	g_chmod (auth_path, (S_IRWXU|S_IRWXG|S_ISVTX));

	NEVER_FAILS_root_set_euid_egid (uid, gid);

	/* Again paranoid */
	check_servauthdir (auth_path, &statbuf);

	if G_UNLIKELY (statbuf.st_uid != 0 || statbuf.st_gid != gid)  {
		if (console_notify) {
			gchar *s = g_strdup_printf (C_(N_("Server Authorization directory "
							  "(daemon/ServAuthDir) is set to %s "
							  "but is not owned by user %d and group "
							  "%d. Please correct the ownership or "
							  "GDM configuration and restart "
							  "GDM.")),
						    auth_path,
						    (int)uid,
						    (int)gid);
			gdm_text_message_dialog (s);
			g_free (s);
		}

		gdm_fail (_("%s: Authdir %s is not owned by user %d, group %d. Aborting."),
			  "gdm_config_parse",
			  auth_path,
			  (int)uid,
			  (int)gid);
	}

	if G_UNLIKELY (statbuf.st_mode != (S_IFDIR|S_IRWXU|S_IRWXG|S_ISVTX))  {
		if (console_notify) {
			gchar *s = g_strdup_printf (C_(N_("Server Authorization directory "
							  "(daemon/ServAuthDir) is set to %s "
							  "but has the wrong permissions: it "
							  "should have permissions of %o. "
							  "Please correct the permissions or "
							  "the GDM configuration and "
							  "restart GDM.")),
						    auth_path,
						    (S_IRWXU|S_IRWXG|S_ISVTX));
			gdm_text_message_dialog (s);
			g_free (s);
		}

		gdm_fail (_("%s: Authdir %s has wrong permissions %o. Should be %o. Aborting."),
			  "gdm_config_parse",
			  auth_path,
			  statbuf.st_mode,
			  (S_IRWXU|S_IRWXG|S_ISVTX));
	}

	g_free (auth_path);
}

/**
 * gdm_daemon_config_parse
 *
 * Loads initial configuration settings.
 */
void
gdm_daemon_config_parse (const char *config_file,
			 gboolean    no_console)
{
	uid_t         uid;
	gid_t         gid;
	GError       *error;
	gboolean      xdmcp_enabled;
	gboolean      dynamic_enabled;

	displays          = NULL;
	high_display_num  = 0;

	/* Not NULL if config_file was set by command-line option. */
	if (config_file == NULL) {
		config_file = GDM_DEFAULTS_CONF;
	}
	custom_config_file = g_strdup (GDM_CUSTOM_CONF);

	daemon_config = gdm_config_new ();

	gdm_config_set_notify_func (daemon_config, notify_cb, NULL);
	gdm_config_set_validate_func (daemon_config, validate_cb, NULL);

	gdm_config_add_static_entries (daemon_config, gdm_daemon_config_entries);

	gdm_config_set_default_file (daemon_config, config_file);
	gdm_config_set_custom_file (daemon_config, custom_config_file);

	/* load the data files */
	error = NULL;
	gdm_config_load (daemon_config, &error);
	if (error != NULL) {
		gdm_error ("Unable to load configuration: %s", error->message);
		g_error_free (error);
	}

	/* populate the database with all specified entries */
	gdm_config_process_all (daemon_config, &error);

	gdm_daemon_config_load_xservers (daemon_config);

	/* Only read the list if no_console is FALSE at this stage */
	if (! no_console) {
		gdm_daemon_config_load_displays (daemon_config);
	}

	xdmcp_enabled = FALSE;
	gdm_config_get_bool_for_id (daemon_config, GDM_ID_XDMCP, &xdmcp_enabled);
	dynamic_enabled = FALSE;
	gdm_config_get_bool_for_id (daemon_config, GDM_ID_DYNAMIC_XSERVERS, &dynamic_enabled);
	if G_UNLIKELY ((displays == NULL) && (! xdmcp_enabled) && (! dynamic_enabled)) {
		handle_no_displays (daemon_config, no_console);
	}

	/* If no displays were found, then obviously
	   we're in a no console mode */
	if (displays == NULL) {
		no_console = TRUE;
	}

	if (no_console) {
		gdm_config_set_bool_for_id (daemon_config, GDM_ID_CONSOLE_NOTIFY, FALSE);
	}

	gdm_daemon_change_user (daemon_config, &uid, &gid);

	gdm_daemon_check_permissions (daemon_config, uid, gid);

	NEVER_FAILS_root_set_euid_egid (0, 0);

	check_logdir ();

	GdmUserId = uid;
	GdmGroupId = gid;

	/* Check that user authentication is properly configured */
	gdm_verify_check ();
}

/**
 * gdm_daemon_config_get_gdmuid
 * gdm_daemon_config_get_gdmgid
 *
 * Access functions for getting the GDM user ID and group ID.
 */
uid_t
gdm_daemon_config_get_gdmuid (void)
{
	return GdmUserId;
}

gid_t
gdm_daemon_config_get_gdmgid (void)
{
	return GdmGroupId;
}

/**
 * gdm_daemon_config_get_high_display_num
 * gdm_daemon_config_get_high_display_num
 *
 * Access functions for getting the high display number.
 */
gint
gdm_daemon_config_get_high_display_num (void)
{
	return high_display_num;
}

void
gdm_daemon_config_set_high_display_num (gint val)
{
	high_display_num = val;
}

/**
 * gdm_is_valid_key
 *
 * Returns TRUE if the key is a valid key, FALSE otherwise.
 */
gboolean
gdm_daemon_config_is_valid_key (const char *keystring)
{
	char    *group;
	char    *key;
	gboolean ret;
	const GdmConfigEntry *entry;

	ret = gdm_common_config_parse_key_string (keystring,
						  &group,
						  &key,
						  NULL,
						  NULL);
	if (! ret) {
		goto out;
	}


	entry = gdm_config_lookup_entry (daemon_config, group, key);
	ret = (entry != NULL);

	g_free (group);
	g_free (key);
 out:
	return ret;
}

/**
 * gdm_signal_terminthup_was_notified
 *
 * returns TRUE if signal SIGTERM, SIGINT, or SIGHUP was received.
 * This just hides these vicious-extensions functions from the
 * other files
 */
gboolean
gdm_daemon_config_signal_terminthup_was_notified (void)
{
	if (ve_signal_was_notified (SIGTERM) ||
	    ve_signal_was_notified (SIGINT) ||
	    ve_signal_was_notified (SIGHUP)) {
		return TRUE;
	} else {
		return FALSE;
	}
}


static gboolean
is_prog_in_path (const char *prog)
{
	char    *f;
	gboolean ret;

	f = g_find_program_in_path (prog);
	ret = (f != NULL);
	g_free (f);
	return ret;
}

/**
 * gdm_daemon_config_get_session_exec
 *
 * This function accesses the GDM session desktop file and returns
 * the execution command for starting the session.
 *
 * Must be called with the PATH set correctly to find session exec.
 */
char *
gdm_daemon_config_get_session_exec (const char *session_name,
				    gboolean    check_try_exec)
{
	char        *session_filename;
	const char  *path_str;
	char       **search_dirs;
	GKeyFile    *cfg;
	static char *exec;
	static char *cached = NULL;
	gboolean     hidden;
	char        *ret;

	cfg = NULL;

	/* clear cache */
	if (session_name == NULL) {
		g_free (exec);
		exec = NULL;
		g_free (cached);
		cached = NULL;
		return NULL;
	}

	if (cached != NULL && strcmp (ve_sure_string (session_name), ve_sure_string (cached)) == 0)
		return g_strdup (exec);

	g_free (exec);
	exec = NULL;
	g_free (cached);
	cached = g_strdup (session_name);

	/* Some ugly special casing for legacy "Default.desktop", oh well,
	 * we changed to "default.desktop" */
	if (g_ascii_strcasecmp (session_name, "default") == 0 ||
	    g_ascii_strcasecmp (session_name, "default.desktop") == 0) {
		session_filename = g_strdup ("default.desktop");
	} else {
		session_filename = gdm_ensure_extension (session_name, ".desktop");
	}

	path_str = gdm_daemon_config_get_value_string (GDM_KEY_SESSION_DESKTOP_DIR);
	if (path_str == NULL) {
		gdm_error ("No session desktop directories defined");
		goto out;
	}

	search_dirs = g_strsplit (path_str, ":", -1);

	cfg = gdm_common_config_load_from_dirs (session_filename,
						(const char **)search_dirs,
						NULL);
	g_strfreev (search_dirs);

	if (cfg == NULL) {
		if (gdm_is_session_magic (session_name)) {
			exec = g_strdup (session_name);
		} else {
			g_free (exec);
			exec = NULL;
		}
		goto out;
	}

	hidden = FALSE;
	gdm_common_config_get_boolean (cfg, "Desktop Entry/Hidden=false", &hidden, NULL);
	if (hidden) {
		g_free (exec);
		exec = NULL;
		goto out;
	}

	if (check_try_exec) {
		char *tryexec;

		tryexec = NULL;
		gdm_common_config_get_string (cfg, "Desktop Entry/TryExec", &tryexec, NULL);

		if (tryexec != NULL &&
		    tryexec[0] != '\0' &&
		    ! is_prog_in_path (tryexec)) {
			g_free (tryexec);
			g_free (exec);
			exec = NULL;
			goto out;
		}
		g_free (tryexec);
	}

	exec = NULL;
	gdm_common_config_get_string (cfg, "Desktop Entry/Exec", &exec, NULL);

 out:

	ret = g_strdup (exec);

	g_key_file_free (cfg);

	return ret;
}

/**
 * gdm_daemon_config_get_session_xserver_args
 *
 * This function accesses the GDM session desktop file and returns
 * additional Xserver arguments to be used with this session
 */
char *
gdm_daemon_config_get_session_xserver_args (const char *session_name)
{
	char        *session_filename;
	const char  *path_str;
	char       **search_dirs;
	GKeyFile    *cfg;
	static char *xserver_args;
	static char *cached = NULL;
	gboolean     hidden;
	char        *ret;

	cfg = NULL;

	/* clear cache */
	if (session_name == NULL) {
		g_free (xserver_args);
		xserver_args = NULL;
		g_free (cached);
		cached = NULL;
		return NULL;
	}

	if (cached != NULL && strcmp (ve_sure_string (session_name), ve_sure_string (cached)) == 0)
		return g_strdup (xserver_args);

	g_free (xserver_args);
	xserver_args = NULL;
	g_free (cached);
	cached = g_strdup (session_name);

	/* Some ugly special casing for legacy "Default.desktop", oh well,
	 * we changed to "default.desktop" */
	if (g_ascii_strcasecmp (session_name, "default") == 0 ||
	    g_ascii_strcasecmp (session_name, "default.desktop") == 0) {
		session_filename = g_strdup ("default.desktop");
	} else {
		session_filename = gdm_ensure_extension (session_name, ".desktop");
	}

	path_str = gdm_daemon_config_get_value_string (GDM_KEY_SESSION_DESKTOP_DIR);
	if (path_str == NULL) {
		gdm_error ("No session desktop directories defined");
		goto out;
	}

	search_dirs = g_strsplit (path_str, ":", -1);

	cfg = gdm_common_config_load_from_dirs (session_filename,
						(const char **)search_dirs,
						NULL);
	g_strfreev (search_dirs);

	xserver_args = NULL;
	gdm_common_config_get_string (cfg, "Desktop Entry/X-Gdm-XserverArgs", &xserver_args, NULL);

 out:

	ret = g_strdup (xserver_args);

	g_key_file_free (cfg);

	return ret;
}

/**
 * gdm_daemon_config_get_user_session_lang
 *
 * These functions get and set the user's language and setting in their
 * $HOME/.dmrc file.
 */
void
gdm_daemon_config_set_user_session_lang (gboolean savesess,
					 gboolean savelang,
					 const char *home_dir,
					 const char *save_session,
					 const char *save_language)
{
	GKeyFile *dmrc;
	gchar *cfgstr;

	cfgstr = g_build_filename (home_dir, ".dmrc", NULL);

	dmrc = gdm_common_config_load (cfgstr, NULL);
	if (dmrc == NULL) {
		return;
	}

	if (savesess) {
		g_key_file_set_string (dmrc, "Desktop", "Session", ve_sure_string (save_session));
	}

	if (savelang) {
		if (ve_string_empty (save_language)) {
			/* we chose the system default language so wipe the
			 * lang key */
			g_key_file_remove_key (dmrc, "Desktop", "Language", NULL);
		} else {
			g_key_file_set_string (dmrc, "Desktop", "Language", save_language);
		}
	}

	if (dmrc != NULL) {
		mode_t oldmode;
		oldmode = umask (077);
		gdm_common_config_save (dmrc, cfgstr, NULL);
		umask (oldmode);
	}

	g_free (cfgstr);
	g_key_file_free (dmrc);
}

void
gdm_daemon_config_get_user_session_lang (char      **usrsess,
					 char      **usrlang,
					 const char *home_dir,
					 gboolean   *savesess)
{
	char *p;
	char *cfgfile;
	GKeyFile *cfg;
	char *session;
	char *lang;
	gboolean save;

	cfgfile = g_build_filename (home_dir, ".dmrc", NULL);
	cfg = gdm_common_config_load (cfgfile, NULL);
	g_free (cfgfile);

	save = FALSE;
	session = NULL;

	gdm_common_config_get_string (cfg, "Desktop/Session", &session, NULL);
	if (session == NULL) {
		session = g_strdup ("");
	}

	/* this is just being truly anal about what users give us, and in case
	 * it looks like they may have included a path whack it. */
	p = strrchr (session, '/');
	if (p != NULL) {
		char *tmp = g_strdup (p + 1);
		g_free (session);
		session = tmp;
	}

	/* ugly workaround for migration */
	if (strcmp (session, "Default") == 0 ||
	    strcmp (session, "Default.desktop") == 0) {
		g_free (session);
		session = g_strdup ("default");
		save = TRUE;
	}

	gdm_common_config_get_string (cfg, "Desktop/Language", &lang, NULL);
	if (lang == NULL) {
		lang = g_strdup ("");
	}

	if (usrsess != NULL) {
		*usrsess = g_strdup (session);
	}
	g_free (session);

	if (savesess != NULL) {
		*savesess = save;
	}

	if (usrlang != NULL) {
		*usrlang = g_strdup (lang);
	}
	g_free (lang);

	g_key_file_free (cfg);
}
