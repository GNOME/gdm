/*
 * This is an utter hack
 *
 * Copyright (C) 2001 Eazel, Inc.
 *
 * Author: George Lebl <jirka@5z.com>
 */


#include "config.h"
#include <gnome.h>

#include "icon-entry-hack.h"

static GtkWidget *
find_iconsel (GtkWidget *box)
{
	GList *list, *li;

	list = gtk_container_children (GTK_CONTAINER (box));

	for (li = list; li != NULL; li = li->next) {
		GtkWidget *w = li->data;
		if (GNOME_IS_ICON_SELECTION (w)) {
			g_list_free (list);
			return w;
		}
	}

	g_list_free (list);
	return NULL;
}

static void
sync_new_to_orig (GnomeIconEntry *ientry)
{
	char *file;
	GnomeIconEntry *origientry;
	GtkWidget *entry;

	origientry = gtk_object_get_data (GTK_OBJECT (ientry),
					  "IconHackOrigIconEntry");

	file = gnome_icon_entry_get_filename (ientry);

	if (origientry != NULL) {
		gtk_object_set_data (GTK_OBJECT (origientry), "IconHackNoSync",
				     GINT_TO_POINTER (1));

		gnome_icon_entry_set_icon (origientry, file);

		entry = gnome_icon_entry_gtk_entry
			(GNOME_ICON_ENTRY (origientry));
		gtk_signal_emit_by_name (GTK_OBJECT (entry), "changed");

		gtk_object_remove_data (GTK_OBJECT (origientry), "IconHackNoSync");
	} else {
		if (file != NULL)
			gtk_object_set_data_full (GTK_OBJECT (ientry),
						  "IconHackText",
						  g_strdup (file),
						  (GtkDestroyNotify) g_free);
		else
			gtk_object_remove_data (GTK_OBJECT (ientry),
						"IconHackText");
		entry = gnome_icon_entry_gtk_entry (GNOME_ICON_ENTRY (ientry));
		gtk_signal_emit_by_name (GTK_OBJECT (entry), "changed");
	}

	g_free (file);
}

static void
sync_orig_to_new (GnomeIconEntry *ientry)
{
	char *file;
	GnomeIconEntry *origientry;

	origientry = gtk_object_get_data (GTK_OBJECT (ientry),
					  "IconHackOrigIconEntry");

	if (origientry != NULL) {
		file = gnome_icon_entry_get_filename (origientry);
	} else {
		file = g_strdup (gtk_object_get_data
				 (GTK_OBJECT (ientry), "IconHackText"));
	}
	gnome_icon_entry_set_icon (ientry, file);
	g_free (file);
}

static void
idle_remove (gpointer data)
{
	guint idle = GPOINTER_TO_UINT (data);

	gtk_idle_remove (idle);
}

static gboolean
sync_orig_to_new_idle (gpointer data)
{
	sync_orig_to_new (data);
	gtk_object_remove_data (GTK_OBJECT (data), "IconHackToNewIdle");
	return FALSE;
}

static void
add_sync_orig_to_new_idle_for_entry (GnomeIconEntry *ientry)
{
	guint idle = GPOINTER_TO_UINT (gtk_object_get_data (GTK_OBJECT (ientry),
							    "IconHackToNewIdle"));
	if (idle != 0)
		return;

	idle = gtk_idle_add (sync_orig_to_new_idle, ientry);

	gtk_object_set_data_full (GTK_OBJECT (ientry), "IconHackToNewIdle",
				  GUINT_TO_POINTER (idle),
				  (GtkDestroyNotify) idle_remove);
}

static gboolean
sync_new_to_orig_idle (gpointer data)
{
	sync_new_to_orig (data);
	gtk_object_remove_data (GTK_OBJECT (data), "IconHackToOrigIdle");
	return FALSE;
}

static void
add_sync_new_to_orig_idle_for_entry (GnomeIconEntry *ientry)
{
	guint idle = GPOINTER_TO_UINT (gtk_object_get_data (GTK_OBJECT (ientry),
							    "IconHackToOrigIdle"));
	if (idle != 0)
		return;

	idle = gtk_idle_add (sync_new_to_orig_idle, ientry);

	gtk_object_set_data_full (GTK_OBJECT (ientry), "IconHackToOrigIdle",
				  GUINT_TO_POINTER (idle),
				  (GtkDestroyNotify) idle_remove);
}

static void
icon_selected (GtkWidget *w, gint num, GdkEvent *event, gpointer data)
{
	if(event &&
	   event->type == GDK_2BUTTON_PRESS &&
	   ((GdkEventButton *)event)->button == 1) {
		sync_new_to_orig (data);
	}
}

static gboolean
delete_event (GtkWidget *w, GdkEvent *event, gpointer data)
{
	GnomeIconEntry *ientry = data;

	gtk_object_set_data (GTK_OBJECT (ientry->pick_dialog),
			     "IconEntryHackCanceled",
			     GINT_TO_POINTER (1));

	sync_orig_to_new (ientry);

	return FALSE;
}

static void
cancel (GtkWidget *w, gpointer data)
{
	GnomeIconEntry *ientry = data;

	gtk_object_set_data (GTK_OBJECT (ientry->pick_dialog),
			     "IconEntryHackCanceled",
			     GINT_TO_POINTER (1));

	sync_orig_to_new (ientry);
}

static void
dialog_hide (GtkWidget *w, gpointer data)
{
	GnomeIconEntry *ientry = data;

	if (gtk_object_get_data (GTK_OBJECT (ientry->pick_dialog),
				 "IconEntryHackCanceled") == NULL) {
		sync_new_to_orig (ientry);
	}
}

static void
shown_icon_selection (GtkWidget *w, gpointer data)
{
	GnomeIconEntry *ientry = data;

	if (ientry->pick_dialog == NULL)
		return;

	gtk_object_remove_data (GTK_OBJECT (ientry->pick_dialog),
				"IconEntryHackCanceled");

	if ( ! gtk_object_get_data
	     (GTK_OBJECT (ientry->pick_dialog), "IconHackDidConnect")) {
		GtkWidget *iconsel;

		gnome_dialog_button_connect_object
			(GNOME_DIALOG (ientry->pick_dialog),
			 0, /* OK button */
			 GTK_SIGNAL_FUNC (sync_new_to_orig),
			 GTK_OBJECT (ientry));
		gnome_dialog_button_connect
			(GNOME_DIALOG (ientry->pick_dialog),
			 1, /* Cancel button */
			 GTK_SIGNAL_FUNC (cancel),
			 ientry);

		iconsel = find_iconsel
			(GNOME_DIALOG (ientry->pick_dialog)->vbox);

		g_assert (iconsel != NULL);

		gtk_signal_connect_after
			(GTK_OBJECT (GNOME_ICON_SELECTION (iconsel)->gil),
			 "select_icon",
			 GTK_SIGNAL_FUNC (icon_selected),
			 ientry);

		gtk_signal_connect (GTK_OBJECT (ientry->pick_dialog),
				    "hide",
				    GTK_SIGNAL_FUNC (dialog_hide),
				    ientry);
		gtk_signal_connect (GTK_OBJECT (ientry->pick_dialog),
				    "delete_event",
				    GTK_SIGNAL_FUNC (delete_event),
				    ientry);

		gtk_object_set_data (GTK_OBJECT (ientry->pick_dialog),
				     "IconHackDidConnect",
				     GINT_TO_POINTER (1));
	}
}

static void
orig_entry_changed (GtkWidget *w, gpointer data)
{
	GnomeIconEntry *ientry = data;
	GnomeIconEntry *origientry;

	origientry = gtk_object_get_data (GTK_OBJECT (ientry),
					  "IconHackOrigIconEntry");

	if ( ! gtk_object_get_data (GTK_OBJECT (origientry), "IconHackNoSync")) {
		add_sync_orig_to_new_idle_for_entry (ientry);
	}
}

void
hack_dentry_edit (GnomeDEntryEdit *dedit)
{
	GtkWidget *ientry;
	GtkWidget *parent;
	GtkWidget *entry;

	g_return_if_fail (GNOME_IS_DENTRY_EDIT (dedit));

	ientry = gnome_icon_entry_new ("icon", _("Choose an icon"));
	gtk_widget_show (ientry);
	gtk_object_set_data (GTK_OBJECT (ientry),
			     "IconHackOrigIconEntry", dedit->icon_entry);

	entry = gnome_icon_entry_gtk_entry
		(GNOME_ICON_ENTRY (dedit->icon_entry));
	gtk_signal_connect (GTK_OBJECT (entry), "changed",
			    GTK_SIGNAL_FUNC (orig_entry_changed),
			    ientry);

	gtk_widget_ref (dedit->icon_entry);

	/* it gets destroyed, so we lose the last refcount then */
	gtk_signal_connect (GTK_OBJECT (dedit->icon_entry), "destroy",
			    GTK_SIGNAL_FUNC (gtk_widget_unref),
			    NULL);

	parent = dedit->icon_entry->parent;

	gtk_container_remove (GTK_CONTAINER (parent), dedit->icon_entry);

	gtk_box_pack_start (GTK_BOX (parent), ientry,
			    FALSE, FALSE, 0);

	gtk_box_reorder_child (GTK_BOX (parent), ientry, 0);

	gtk_signal_connect (GTK_OBJECT (GNOME_ICON_ENTRY (ientry)->pickbutton),
			    "clicked",
			   GTK_SIGNAL_FUNC (shown_icon_selection), ientry);

	sync_orig_to_new (GNOME_ICON_ENTRY (ientry));
}
	

void
hack_icon_entry (GnomeIconEntry *ientry)
{
	gtk_signal_connect (GTK_OBJECT (GNOME_ICON_ENTRY (ientry)->pickbutton),
			    "clicked",
			    GTK_SIGNAL_FUNC (shown_icon_selection),
			    ientry);

	add_sync_new_to_orig_idle_for_entry (ientry);
}

char *
hack_icon_entry_get_icon (GnomeIconEntry *ientry)
{
	return g_strdup (gtk_object_get_data (GTK_OBJECT (ientry),
					      "IconHackText"));
}

void
hack_icon_entry_set_icon (GnomeIconEntry *ientry, const char *icon)
{
	gnome_icon_entry_set_icon (ientry, icon);
	add_sync_new_to_orig_idle_for_entry (ientry);
}
