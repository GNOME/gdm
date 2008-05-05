/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2008  Red Hat, Inc,
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
 * Written by : Matthias Clasen
 */

#include "config.h"

#include <string.h>

#include <glib.h>

#include <gdk/gdkx.h>
#include <libxklavier/xklavier.h>
#include <gconf/gconf-client.h>

#include "gdm-layouts.h"

static XklEngine *engine = NULL;
static XklConfigRegistry *config_registry = NULL;
static XklConfigRec *initial_config = NULL;

static void
init_xkl (void)
{
	if (config_registry == NULL) {
		engine = xkl_engine_get_instance (GDK_DISPLAY ());
		xkl_engine_backup_names_prop (engine);
		config_registry = xkl_config_registry_get_instance (engine);
		xkl_config_registry_load (config_registry);

		initial_config = xkl_config_rec_new ();
		if (!xkl_config_rec_get_from_backup (initial_config, engine)) {
			g_warning ("failed to load XKB configuration");
			initial_config->model = g_strdup ("pc105");
		}
	}
}

static char *
xci_desc_to_utf8 (XklConfigItem * ci)
{
        char *sd = g_strstrip (ci->description);
        return sd[0] == 0 ? g_strdup (ci->name) :
            g_locale_to_utf8 (sd, -1, NULL, NULL, NULL);
}

gchar *
gdm_get_layout_from_name (const char *name)
{
	XklConfigItem *item;
	gchar *layout, *variant, *result;
	char *id1, *id2, *p;

	init_xkl ();

	id1 = g_strdup (name);
	p = strchr (id1, '\t');

	if (p) {
		id2 = p + 1;
		*p = 0;
	}
	else 
		id2 = NULL;

	item = xkl_config_item_new ();

	g_snprintf (item->name, XKL_MAX_CI_NAME_LENGTH, id1);
	if (xkl_config_registry_find_layout (config_registry, item))
		layout = xci_desc_to_utf8 (item);
	else
		layout =  g_strdup_printf ("Layout %s", id1);

	if (id2) {
		g_snprintf (item->name, XKL_MAX_CI_NAME_LENGTH, id2);
		if (xkl_config_registry_find_variant (config_registry, id1, item))
			variant = xci_desc_to_utf8 (item);
		else
			variant = g_strdup_printf ("Variant %s", id2);
	}
	else 
		variant = NULL;

	g_object_unref (item);

	g_free (id1);

	if (variant) {
		result = g_strdup_printf ("%s (%s)", layout, variant);
		g_free (layout);
		g_free (variant);
	}
	else
		result = layout;

	return result;
}

typedef struct {
	GSList *list;
	char *layout;
} LayoutData;

static void
add_variant (XklConfigRegistry   *config,
             const XklConfigItem *item,
             gpointer             data)
{
	LayoutData *ldata = data;

	ldata->list = g_slist_prepend (ldata->list, g_strdup_printf  ("%s\t%s", ldata->layout, item->name));
}

static void
add_layout (XklConfigRegistry   *config,
            const XklConfigItem *item,
            gpointer             data)
{
	LayoutData *ldata = data;

	ldata->layout = item->name;
	ldata->list = g_slist_prepend (ldata->list, g_strdup (item->name));
	xkl_config_registry_foreach_layout_variant (config, item->name, add_variant, data);
	ldata->layout = NULL;
}

char **
gdm_get_all_layout_names (void)
{
	GSList *l;
	int len, i;
        char **layouts;
	LayoutData data;

	init_xkl ();

	data.list = NULL;
	data.layout = NULL;

	xkl_config_registry_foreach_layout (config_registry, add_layout, &data);

	len = g_slist_length (data.list);

        layouts = g_new (char *, len + 1);
	layouts[len] = NULL;

	for (i = 0, l = data.list; i < len; i++, l = l->next) 
		layouts[len - i - 1] = l->data;

	g_slist_free (data.list);

        return layouts;
}

void
gdm_layout_activate (const char *layout)
{
	XklConfigRec *config;
	char *p;

	init_xkl ();

	config = xkl_config_rec_new ();
	config->model = g_strdup (initial_config->model);

	if (layout == NULL) {
		config->layouts = g_strdupv (initial_config->layouts);
		config->variants = g_strdupv (initial_config->variants);
		config->options = g_strdupv (initial_config->options);
	}
	else {
		config->layouts = g_new0 (gchar *, 2);
		config->layouts[0] = g_strdup (layout);

		p = strchr (config->layouts[0], '\t');
		if (p) {
				
			config->variants = g_new0 (gchar *, 2);
			config->layouts[0][p - config->layouts[0]] = 0;
			config->variants[0] = g_strdup (p + 1);	
		}
	}

	xkl_config_rec_activate (config, engine);
	
	g_object_unref (config);
}

