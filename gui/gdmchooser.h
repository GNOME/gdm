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

#ifndef __GDM_CHOOSER_H__
#define __GDM_CHOOSER_H__

#include <gnome.h>

/* If you (for some odd reason) have more than 16 interfaces in your
 * machine, redefine this */

#define MAXIF 16


typedef struct _GdmChooserHost GdmChooserHost;

struct _GdmChooserHost {
    gchar *name;
    gchar *desc;
    struct in_addr ia;
    GdkImlibImage *picture;
    gboolean willing;
};

gboolean gdm_chooser_cancel (void);
gboolean gdm_chooser_manage (GtkButton *button, gpointer data);
gboolean 
gdm_chooser_browser_select (GtkWidget *widget, gint selected, GdkEvent *event);
gboolean
gdm_chooser_browser_unselect (GtkWidget *widget, gint selected, GdkEvent *event);
gboolean 
gdm_chooser_xdmcp_discover (void);
void
display_chooser_information (void);

#endif /* __GDM_CHOOSER_H__ */

/* EOF */
