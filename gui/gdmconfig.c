/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999 Martin Kasper Petersen <mkp@SunSITE.auc.dk>
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

/* gdmconfig is a visual user interface for configuring the gdm package */

#include <config.h>
#include <gnome.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../daemon/gdm.h"

static const gchar RCSid[]="$Id$";

/* Kaboom */
static GtkWidget *pbox = NULL;
static GtkWidget *apppage = NULL;
static GtkWidget *iconframe = NULL;
static GtkWidget *iconentry = NULL;
static GtkWidget *lookframe = NULL;
static GtkWidget *lookbox = NULL;
static GtkWidget *welcomelab = NULL;
static GtkWidget *welcomentry = NULL;
static GtkWidget *themelab = NULL;
static GtkWidget *themefentry = NULL;
static GtkWidget *quiver = NULL;
static GtkWidget *logoframe = NULL;
static GtkWidget *logopentry = NULL;
static GtkWidget *browser = NULL;
static GtkWidget *browserpage = NULL;
static GtkWidget *browsertable = NULL;
static GtkWidget *faceframe = NULL;
static GtkWidget *facepentry = NULL;
static GtkWidget *advframe = NULL;
static GtkWidget *advtable = NULL;
static GtkWidget *maxsizelab = NULL;
static GtkWidget *maxsizeentry = NULL;
static GtkWidget *maxwentry = NULL;
static GtkWidget *maxhentry = NULL;
static GtkWidget *maxwlab = NULL;
static GtkWidget *maxhlab = NULL;
static GtkWidget *facedirframe = NULL;
static GtkWidget *facedirfentry = NULL;
static GtkWidget *userpage = NULL;
static GtkWidget *usertable = NULL;
static GtkWidget *includelab = NULL;
static GtkWidget *excludelab = NULL;
static GtkWidget *excludeclist = NULL;
static GtkWidget *includeclist = NULL;
static GtkWidget *userbox = NULL;
static GtkWidget *includebutton = NULL;
static GtkWidget *excludebutton = NULL;
static GtkWidget *systempage = NULL;
static GtkWidget *stateframe = NULL;
static GtkWidget *statebox = NULL;
static GtkWidget *sysmenu = NULL;
static GtkWidget *suspendlab = NULL;
static GtkWidget *suspentry = NULL;
static GtkWidget *envframe = NULL;
static GtkWidget *envbox = NULL;
static GtkWidget *pathlab = NULL;
static GtkWidget *pathentry = NULL;
static GtkWidget *killinitclients = NULL;
static GtkWidget *securityframe = NULL;
static GtkWidget *securitytable = NULL;
static GtkObject *delayentry_adj = NULL;
static GtkWidget *delayentry = NULL;
static GtkWidget *acceptlab = NULL;
static GtkWidget *delaylab = NULL;
static GtkWidget *allowroot = NULL;
static GtkWidget *verboseauth = NULL;
static GtkWidget *acceptmenu = NULL;
static GtkWidget *acceptmenu_menu = NULL;
static GtkWidget *menuitem = NULL;
static GtkWidget *serverpage = NULL;
static GtkWidget *servertable = NULL;
static GtkWidget *serverlist = NULL;
static GtkWidget *slnumlab = NULL;
static GtkWidget *slcmdlab = NULL;
static GtkWidget *servbbox = NULL;
static GtkWidget *servadd = NULL;
static GtkWidget *servdel = NULL;
static GtkWidget *servact = NULL;
static GtkWidget *xdmcppage = NULL;
static GtkWidget *xdmcp = NULL;
static GtkWidget *xdmcptable = NULL;
static GtkWidget *protoframe = NULL;
static GtkWidget *prototable = NULL;
static GtkObject *maxpentry_adj = NULL;
static GtkWidget *maxpentry = NULL;
static GtkObject *maxmanagentry_adj = NULL;
static GtkWidget *maxmanagentry = NULL;
static GtkObject *maxrentry_adj = NULL;
static GtkWidget *maxrentry = NULL;
static GtkWidget *udpentry = NULL;
static GtkWidget *maxplab = NULL;
static GtkWidget *maxmanlab = NULL;
static GtkWidget *maxrlab = NULL;
static GtkWidget *udplab = NULL;
static GtkWidget *chooserframe = NULL;
static GtkWidget *chooserbox = NULL;
static GtkWidget *hostimglab = NULL;
static GtkWidget *hostimgfentry = NULL;
static GtkWidget *defhostlab = NULL;
static GtkWidget *defhostpentry = NULL;

gchar  *GdmLogoFilename;
gchar  *GdmWelcomeMessage;
gchar  *GdmGtkRC;
gint   GdmQuiver;
gchar  *GdmIconFile;
gint   GdmDisplayBrowser;
gchar  *GdmDefaultFace;
gchar  *GdmGlobalFaceDir;
gint   GdmUserMaxFile;
gint   GdmIconMaxWidth;
gint   GdmIconMaxHeight;
gchar  *GdmExcludeUsers;
gint   GdmSystemMenu;
gchar  *GdmSuspend;
gchar  *GdmDefaultPath;
gint   GdmKillInitClients;
gint   GdmAllowRoot;
gint   GdmVerboseAuth;
gint   GdmRetryDelay;
gint   GdmRelaxPerms;
gint   GdmXdmcp;
gint   GdmMaxPending;
gint   GdmMaxManageWait;
gint   GdmMaxSessions;


GList  *xusers = NULL;


static int
gdm_config_cancel (void)
{
    return (FALSE);
}


static int
gdm_config_apply (void)
{
    return (FALSE);
}


static void
gdm_config_parse ()
{
    gchar *k, *v;
    void *iter;
    struct stat statbuf;
    gchar *tmp;
    
    /* FIXME */
    if (stat (GDM_CONFIG_FILE, &statbuf) == -1)
	exit(EXIT_FAILURE);

    gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");

    /* Appearance */
    GdmLogoFilename = gnome_config_get_string (GDM_KEY_LOGO);

    if(GdmLogoFilename) {
	GtkWidget *entry = gnome_pixmap_entry_gtk_entry (GNOME_PIXMAP_ENTRY (logopentry));
	gtk_entry_set_text (GTK_ENTRY (entry), GdmLogoFilename);
    }


    GdmWelcomeMessage = gnome_config_get_string (GDM_KEY_WELCOME);

    if (GdmWelcomeMessage) 
	gtk_entry_set_text (GTK_ENTRY (welcomentry), GdmWelcomeMessage);


    GdmGtkRC = gnome_config_get_string (GDM_KEY_GTKRC);

    if(GdmGtkRC) {
	GtkWidget *entry = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (themefentry));
	gtk_entry_set_text (GTK_ENTRY (entry), GdmGtkRC);
    }


    GdmQuiver = gnome_config_get_int (GDM_KEY_QUIVER);

    if(GdmQuiver)
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (quiver), TRUE);
    else
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (quiver), FALSE);


    GdmIconFile = gnome_config_get_string (GDM_KEY_ICON);

    if(GdmIconFile)
	gnome_icon_entry_set_icon (GNOME_ICON_ENTRY (iconentry), GdmIconFile);


    /* Browser */
    GdmDisplayBrowser = gnome_config_get_int (GDM_KEY_BROWSER);

    if (GdmDisplayBrowser) {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (browser), TRUE);
	gtk_widget_set_sensitive (GTK_WIDGET (browsertable), TRUE);
    }
    else {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (browser), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (browsertable), FALSE);
    }


    GdmDefaultFace = gnome_config_get_string (GDM_KEY_FACE);

    if (GdmDefaultFace) {
	GtkWidget *entry = gnome_pixmap_entry_gtk_entry (GNOME_PIXMAP_ENTRY (facepentry));
	gtk_entry_set_text (GTK_ENTRY (entry), GdmDefaultFace);
    }


    GdmGlobalFaceDir = gnome_config_get_string (GDM_KEY_FACEDIR);

    if (GdmGlobalFaceDir) {
	GtkWidget *entry = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (facedirfentry));
	gtk_entry_set_text (GTK_ENTRY (entry), GdmGlobalFaceDir);
    }


    GdmUserMaxFile = gnome_config_get_int (GDM_KEY_MAXFILE);
    tmp = g_strdup_printf ("%d", GdmUserMaxFile);
    gtk_entry_set_text (GTK_ENTRY (maxsizeentry), tmp);
    g_free(tmp);

    GdmIconMaxWidth = gnome_config_get_int (GDM_KEY_ICONWIDTH);
    tmp = g_strdup_printf ("%d", GdmIconMaxWidth);
    gtk_entry_set_text (GTK_ENTRY (maxwentry), tmp);
    g_free(tmp);

    GdmIconMaxHeight = gnome_config_get_int (GDM_KEY_ICONHEIGHT);
    tmp = g_strdup_printf ("%d", GdmIconMaxHeight);
    gtk_entry_set_text (GTK_ENTRY (maxhentry), tmp);
    g_free(tmp);

    /* Users */
    GdmExcludeUsers = gnome_config_get_string (GDM_KEY_EXCLUDE);

    if (GdmExcludeUsers) {
	gchar *array[1];

	gtk_clist_freeze (GTK_CLIST (excludeclist));

	array[0] = strtok (GdmExcludeUsers, ",");
	gtk_clist_append (GTK_CLIST (excludeclist), array);

  	while ((array[0] = strtok (NULL, ",")))
	    gtk_clist_append (GTK_CLIST (excludeclist), array);

	gtk_clist_thaw (GTK_CLIST (excludeclist));
    }


    /* System */
    GdmSystemMenu = gnome_config_get_int (GDM_KEY_SYSMENU);

    if (GdmSystemMenu) {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sysmenu), TRUE);
	gtk_widget_set_sensitive (GTK_WIDGET (suspentry), TRUE);
    }
    else {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sysmenu), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (suspentry), FALSE);
    }


/*    GdmSuspend = gnome_config_get_string(GDM_KEY_SUSPEND);*/

    if (GdmSuspend) 
	gtk_entry_set_text (GTK_ENTRY (suspentry), GdmSuspend);


    GdmDefaultPath = gnome_config_get_string(GDM_KEY_PATH);

    if (GdmDefaultPath)
	gtk_entry_set_text (GTK_ENTRY (pathentry), GdmDefaultPath);


    GdmKillInitClients = gnome_config_get_int (GDM_KEY_KILLIC);

    if (GdmKillInitClients)
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (killinitclients), TRUE);
    else
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (killinitclients), FALSE);

    GdmAllowRoot = gnome_config_get_int (GDM_KEY_ALLOWROOT);

    if (GdmAllowRoot)
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (allowroot), TRUE);
    else
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (allowroot), FALSE);

    GdmVerboseAuth = gnome_config_get_int (GDM_KEY_VERBAUTH);

    if (GdmVerboseAuth)
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (verboseauth), TRUE);
    else
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (verboseauth), FALSE);

    GdmRetryDelay = gnome_config_get_int (GDM_KEY_RETRYDELAY);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (delayentry), GdmRetryDelay);

    GdmRelaxPerms = gnome_config_get_int (GDM_KEY_RELAXPERM);
    /* FIXME: Do menu stuff */
    

    /* XDMCP */
    GdmXdmcp = gnome_config_get_int (GDM_KEY_XDMCP);

    if (GdmXdmcp) {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (xdmcp), TRUE);
	gtk_widget_set_sensitive (GTK_WIDGET (xdmcptable), TRUE);
    }
    else {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (xdmcp), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (xdmcptable), FALSE);
    }


    GdmMaxPending = gnome_config_get_int (GDM_KEY_MAXPEND);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (maxpentry), GdmMaxPending);    

    GdmMaxManageWait = gnome_config_get_int (GDM_KEY_MAXWAIT);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (maxmanagentry), GdmMaxManageWait);    

    GdmMaxSessions = gnome_config_get_int (GDM_KEY_MAXSESS);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (maxrentry), GdmMaxSessions);    

    gnome_config_pop_prefix();

    /* X Servers */
/*    iter=gnome_config_init_iterator("=" GDM_CONFIG_FILE "=/" GDM_KEY_SERVERS);
    iter=gnome_config_iterator_next (iter, &k, &v);
    
    while (iter) {

	if(isdigit(*k))
	    displays=g_slist_append(displays, gdm_server_alloc(atoi(k), v));
	else
	    gdm_info(_("gdm_config_parse: Invalid server line in config file. Ignoring!"));

	iter=gnome_config_iterator_next (iter, &k, &v);
    }
*/
}


static void
gdm_config_gui_init (void)
{
    pbox = gnome_property_box_new ();
    gtk_window_set_title (GTK_WINDOW (pbox), _("GDM Configuration"));
/*    gnome_dialog_set_parent (GNOME_DIALOG (pbox), GTK_WINDOW (toplevel));*/
        
    gtk_signal_connect (GTK_OBJECT (pbox), "destroy",
			(GtkSignalFunc) gdm_config_cancel, NULL);
    
    gtk_signal_connect (GTK_OBJECT (pbox), "delete_event",
			(GtkSignalFunc) gdm_config_cancel, NULL);

    gtk_signal_connect (GTK_OBJECT (pbox), "apply",
			(GtkSignalFunc) gdm_config_apply, NULL);

    apppage = gtk_table_new (2, 2, FALSE);
    gtk_widget_ref (apppage);

    gtk_object_set_data_full (GTK_OBJECT (pbox), "apppage", apppage,
			      (GtkDestroyNotify) gtk_widget_unref);

    gnome_property_box_append_page (GNOME_PROPERTY_BOX (pbox), apppage,
				    gtk_label_new (_("Appearance")));

    gtk_container_set_border_width (GTK_CONTAINER (apppage), 10);
    gtk_table_set_row_spacings (GTK_TABLE (apppage), 10);
    gtk_table_set_col_spacings (GTK_TABLE (apppage), 10);

    gtk_widget_show (apppage);
    
    iconframe = gtk_frame_new (_("Icon"));
    gtk_widget_ref (iconframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "iconframe", iconframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (iconframe);
    gtk_table_attach (GTK_TABLE (apppage), iconframe, 1, 2, 1, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 0);
    
    iconentry = gnome_icon_entry_new (NULL, NULL);
    gtk_widget_ref (iconentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "iconentry", iconentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (iconentry);
    gtk_container_add (GTK_CONTAINER (iconframe), iconentry);
    
    lookframe = gtk_frame_new (_("Look and feel"));
    gtk_widget_ref (lookframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "lookframe", lookframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (lookframe);
    gtk_table_attach (GTK_TABLE (apppage), lookframe, 1, 2, 0, 1,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 0);
    
    lookbox = gtk_vbox_new (FALSE, 10);
    gtk_widget_ref (lookbox);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "lookbox", lookbox,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (lookbox);
    gtk_container_add (GTK_CONTAINER (lookframe), lookbox);
    gtk_container_set_border_width (GTK_CONTAINER (lookbox), 10);
    
    welcomelab = gtk_label_new (_("Welcome message:"));
    gtk_widget_ref (welcomelab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "welcomelab", welcomelab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (welcomelab);
    gtk_box_pack_start (GTK_BOX (lookbox), welcomelab, FALSE, FALSE, 0);
    gtk_label_set_justify (GTK_LABEL (welcomelab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (welcomelab), 0, 0.5);
    
    welcomentry = gtk_entry_new ();
    gtk_widget_ref (welcomentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "welcomentry", welcomentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (welcomentry);
    gtk_box_pack_start (GTK_BOX (lookbox), welcomentry, FALSE, FALSE, 0);

    themelab = gtk_label_new (_("Theme:"));
    gtk_widget_ref (themelab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "themelab", themelab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (themelab);
    gtk_box_pack_start (GTK_BOX (lookbox), themelab, FALSE, FALSE, 0);
    gtk_label_set_justify (GTK_LABEL (themelab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (themelab), 0, 0.5);
    
    themefentry = gnome_file_entry_new (NULL, NULL);
    gtk_widget_ref (themefentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "themefentry", themefentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (themefentry);
    gtk_box_pack_start (GTK_BOX (lookbox), themefentry, FALSE, FALSE, 0);
    
    quiver = gtk_check_button_new_with_label (_("Quiver on failure"));
    gtk_widget_ref (quiver);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "quiver", quiver,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (quiver);
    gtk_box_pack_start (GTK_BOX (lookbox), quiver, FALSE, FALSE, 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (quiver), TRUE);
    
    logoframe = gtk_frame_new (_("Logo"));
    gtk_widget_ref (logoframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "logoframe", logoframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (logoframe);
    gtk_table_attach (GTK_TABLE (apppage), logoframe, 0, 1, 0, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
    
    logopentry = gnome_pixmap_entry_new (NULL, NULL, TRUE);
    gtk_widget_ref (logopentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "logopentry", logopentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (logopentry);
    gtk_container_add (GTK_CONTAINER (logoframe), logopentry);
    gtk_container_set_border_width (GTK_CONTAINER (logopentry), 10);
        
    browserpage = gtk_vbox_new (FALSE, 10);
    gtk_widget_ref (browserpage);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "browserpage", browserpage,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (browserpage);
    gtk_container_set_border_width (GTK_CONTAINER (browserpage), 10);

    gnome_property_box_append_page (GNOME_PROPERTY_BOX (pbox), browserpage,
				    gtk_label_new (_("Browser")));
    
    browser = gtk_check_button_new_with_label (_("Enable face browser"));
    gtk_widget_ref (browser);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "browser", browser,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (browser);
    gtk_box_pack_start (GTK_BOX (browserpage), browser, FALSE, FALSE, 0);
    
    browsertable = gtk_table_new (2, 2, FALSE);
    gtk_widget_ref (browsertable);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "browsertable", browsertable,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (browsertable);
    gtk_box_pack_start (GTK_BOX (browserpage), browsertable, TRUE, TRUE, 0);
    gtk_table_set_row_spacings (GTK_TABLE (browsertable), 10);
    gtk_table_set_col_spacings (GTK_TABLE (browsertable), 10);
    
    faceframe = gtk_frame_new (_("Default face image"));
    gtk_widget_ref (faceframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "faceframe", faceframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (faceframe);
    gtk_table_attach (GTK_TABLE (browsertable), faceframe, 0, 1, 0, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
    
    facepentry = gnome_pixmap_entry_new (NULL, NULL, TRUE);
    gtk_widget_ref (facepentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "facepentry", facepentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (facepentry);
    gtk_container_add (GTK_CONTAINER (faceframe), facepentry);
    gtk_container_set_border_width (GTK_CONTAINER (facepentry), 10);
    
    advframe = gtk_frame_new (_("Advanced"));
    gtk_widget_ref (advframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "advframe", advframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (advframe);
    gtk_table_attach (GTK_TABLE (browsertable), advframe, 1, 2, 1, 2,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 0);
    
    advtable = gtk_table_new (3, 2, FALSE);
    gtk_widget_ref (advtable);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "advtable", advtable,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (advtable);
    gtk_container_add (GTK_CONTAINER (advframe), advtable);
    gtk_container_set_border_width (GTK_CONTAINER (advtable), 10);
    gtk_table_set_row_spacings (GTK_TABLE (advtable), 10);
    gtk_table_set_col_spacings (GTK_TABLE (advtable), 10);
    
    maxsizelab = gtk_label_new (_("Max icon file size (bytes):"));
    gtk_widget_ref (maxsizelab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxsizelab", maxsizelab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxsizelab);
    gtk_table_attach (GTK_TABLE (advtable), maxsizelab, 0, 1, 0, 1,
		      (GtkAttachOptions) (0),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_misc_set_alignment (GTK_MISC (maxsizelab), 0, 0.5);
    
    maxsizeentry = gtk_entry_new ();
    gtk_widget_ref (maxsizeentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxsizeentry", maxsizeentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxsizeentry);
    gtk_table_attach (GTK_TABLE (advtable), maxsizeentry, 1, 2, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    
    maxwentry = gtk_entry_new ();
    gtk_widget_ref (maxwentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxwentry", maxwentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxwentry);
    gtk_table_attach (GTK_TABLE (advtable), maxwentry, 1, 2, 1, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    
    maxhentry = gtk_entry_new ();
    gtk_widget_ref (maxhentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxhentry", maxhentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxhentry);
    gtk_table_attach (GTK_TABLE (advtable), maxhentry, 1, 2, 2, 3,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    
    maxwlab = gtk_label_new (_("Max face width (pixels):"));
    gtk_widget_ref (maxwlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxwlab", maxwlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxwlab);
    gtk_table_attach (GTK_TABLE (advtable), maxwlab, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify (GTK_LABEL (maxwlab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (maxwlab), 0, 0.5);
    
    maxhlab = gtk_label_new (_("Max face height (pixels):"));
    gtk_widget_ref (maxhlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxhlab", maxhlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxhlab);
    gtk_table_attach (GTK_TABLE (advtable), maxhlab, 0, 1, 2, 3,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify (GTK_LABEL (maxhlab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (maxhlab), 0, 0.5);
    
    facedirframe = gtk_frame_new (_("Global face directory"));
    gtk_widget_ref (facedirframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "facedirframe", facedirframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (facedirframe);
    gtk_table_attach (GTK_TABLE (browsertable), facedirframe, 1, 2, 0, 1,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 0);
    
    facedirfentry = gnome_file_entry_new (NULL, NULL);
    gnome_file_entry_set_directory (GNOME_FILE_ENTRY (facedirfentry), TRUE);
    gtk_widget_ref (facedirfentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "facedirfentry", facedirfentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (facedirfentry);
    gtk_container_add (GTK_CONTAINER (facedirframe), facedirfentry);
    gtk_container_set_border_width (GTK_CONTAINER (facedirfentry), 10);
        
    userpage = gtk_frame_new (_("User display options"));
    gtk_widget_ref (userpage);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "userpage", userpage,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (userpage);

    gtk_container_set_border_width (GTK_CONTAINER (userpage), 10);

    gnome_property_box_append_page (GNOME_PROPERTY_BOX (pbox), userpage,
				    gtk_label_new (_("Users")));
    
    usertable = gtk_table_new (1, 3, FALSE);
    gtk_widget_ref (usertable);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "usertable", usertable,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (usertable);
    gtk_container_add (GTK_CONTAINER (userpage), usertable);
    gtk_container_set_border_width (GTK_CONTAINER (usertable), 10);
    
    userbox = gtk_vbutton_box_new ();
    gtk_widget_ref (userbox);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "userbox", userbox,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (userbox);
    gtk_table_attach (GTK_TABLE (usertable), userbox, 1, 2, 0, 1,
		      (GtkAttachOptions) (0),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (userbox), GTK_BUTTONBOX_SPREAD);
    
    includebutton = gtk_button_new_with_label (_("<<"));
    gtk_widget_ref (includebutton);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "includebutton", includebutton,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (includebutton);
    gtk_container_add (GTK_CONTAINER (userbox), includebutton);
    GTK_WIDGET_SET_FLAGS (includebutton, GTK_CAN_DEFAULT);
    
    excludebutton = gtk_button_new_with_label (_(">>"));
    gtk_widget_ref (excludebutton);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "excludebutton", excludebutton,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (excludebutton);
    gtk_container_add (GTK_CONTAINER (userbox), excludebutton);
    GTK_WIDGET_SET_FLAGS (excludebutton, GTK_CAN_DEFAULT);
    
    includeclist = gtk_clist_new (1);
    gtk_widget_ref (includeclist);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "includeclist", includeclist,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (includeclist);
    gtk_table_attach (GTK_TABLE (usertable), includeclist, 0, 1, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
    gtk_clist_set_column_width (GTK_CLIST (includeclist), 0, 80);
    gtk_clist_set_selection_mode (GTK_CLIST (includeclist), GTK_SELECTION_MULTIPLE);
    gtk_clist_column_titles_show (GTK_CLIST (includeclist));
    
    includelab = gtk_label_new (_("Include"));
    gtk_widget_ref (includelab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "includelab", includelab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (includelab);
    gtk_clist_set_column_widget (GTK_CLIST (includeclist), 0, includelab);
    
    excludeclist = gtk_clist_new (1);
    gtk_widget_ref (excludeclist);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "excludeclist", excludeclist,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (excludeclist);
    gtk_table_attach (GTK_TABLE (usertable), excludeclist, 2, 3, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
    gtk_clist_set_column_width (GTK_CLIST (excludeclist), 0, 80);
    gtk_clist_set_selection_mode (GTK_CLIST (excludeclist), GTK_SELECTION_MULTIPLE);
    gtk_clist_column_titles_show (GTK_CLIST (excludeclist));
    
    excludelab = gtk_label_new (_("Exclude"));
    gtk_widget_ref (excludelab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "excludelab", excludelab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (excludelab);
    gtk_clist_set_column_widget (GTK_CLIST (excludeclist), 0, excludelab);
    
    systempage = gtk_table_new (2, 2, FALSE);
    gtk_widget_ref (systempage);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "systempage", systempage,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (systempage);
    gtk_container_set_border_width (GTK_CONTAINER (systempage), 10);
    gtk_table_set_row_spacings (GTK_TABLE (systempage), 10);
    gtk_table_set_col_spacings (GTK_TABLE (systempage), 10);

    gnome_property_box_append_page (GNOME_PROPERTY_BOX (pbox), systempage,
				    gtk_label_new (_("System")));
    
    stateframe = gtk_frame_new (_("System state"));
    gtk_widget_ref (stateframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "stateframe", stateframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (stateframe);
    gtk_table_attach (GTK_TABLE (systempage), stateframe, 0, 1, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
    
    statebox = gtk_vbox_new (FALSE, 10);
    gtk_widget_ref (statebox);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "statebox", statebox,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (statebox);
    gtk_container_add (GTK_CONTAINER (stateframe), statebox);
    gtk_container_set_border_width (GTK_CONTAINER (statebox), 10);
    
    sysmenu = gtk_check_button_new_with_label (_("Enable system menu"));
    gtk_widget_ref (sysmenu);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "sysmenu", sysmenu,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (sysmenu);
    gtk_box_pack_start (GTK_BOX (statebox), sysmenu, FALSE, FALSE, 0);
    
    suspendlab = gtk_label_new (_("Suspend command:"));
    gtk_widget_ref (suspendlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "suspendlab", suspendlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (suspendlab);
    gtk_box_pack_start (GTK_BOX (statebox), suspendlab, FALSE, FALSE, 0);
    gtk_label_set_justify (GTK_LABEL (suspendlab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (suspendlab), 0, 0.5);
    
    suspentry = gtk_entry_new ();
    gtk_widget_ref (suspentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "suspentry", suspentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (suspentry);
    gtk_box_pack_start (GTK_BOX (statebox), suspentry, FALSE, FALSE, 0);
    
    envframe = gtk_frame_new (_("User environment"));
    gtk_widget_ref (envframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "envframe", envframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (envframe);
    gtk_table_attach (GTK_TABLE (systempage), envframe, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
    
    envbox = gtk_vbox_new (FALSE, 10);
    gtk_widget_ref (envbox);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "envbox", envbox,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (envbox);
    gtk_container_add (GTK_CONTAINER (envframe), envbox);
    gtk_container_set_border_width (GTK_CONTAINER (envbox), 10);
    
    pathlab = gtk_label_new (_("Default path:"));
    gtk_widget_ref (pathlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "pathlab", pathlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (pathlab);
    gtk_box_pack_start (GTK_BOX (envbox), pathlab, FALSE, FALSE, 0);
    gtk_label_set_justify (GTK_LABEL (pathlab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (pathlab), 0, 0.5);
    
    pathentry = gtk_entry_new ();
    gtk_widget_ref (pathentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "pathentry", pathentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (pathentry);
    gtk_box_pack_start (GTK_BOX (envbox), pathentry, FALSE, FALSE, 0);
    
    killinitclients = gtk_check_button_new_with_label (_("Kill init script clients"));
    gtk_widget_ref (killinitclients);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "killinitclients", killinitclients,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (killinitclients);
    gtk_box_pack_start (GTK_BOX (envbox), killinitclients, FALSE, FALSE, 0);
    
    securityframe = gtk_frame_new (_("Security"));
    gtk_widget_ref (securityframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "securityframe", securityframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (securityframe);
    gtk_table_attach (GTK_TABLE (systempage), securityframe, 1, 2, 0, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 0);
    
    securitytable = gtk_table_new (4, 2, FALSE);
    gtk_widget_ref (securitytable);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "securitytable", securitytable,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (securitytable);
    gtk_container_add (GTK_CONTAINER (securityframe), securitytable);
    gtk_container_set_border_width (GTK_CONTAINER (securitytable), 10);
    gtk_table_set_row_spacings (GTK_TABLE (securitytable), 10);
    
    delayentry_adj = gtk_adjustment_new (3, 0, 100, 1, 10, 10);
    delayentry = gtk_spin_button_new (GTK_ADJUSTMENT (delayentry_adj), 1, 0);
    gtk_widget_ref (delayentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "delayentry", delayentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (delayentry);
    gtk_table_attach (GTK_TABLE (securitytable), delayentry, 1, 2, 2, 3,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (delayentry), TRUE);
    
    acceptlab = gtk_label_new (_("Accept files writable by:"));
    gtk_widget_ref (acceptlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "acceptlab", acceptlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (acceptlab);
    gtk_table_attach (GTK_TABLE (securitytable), acceptlab, 0, 1, 3, 4,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify (GTK_LABEL (acceptlab), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap (GTK_LABEL (acceptlab), TRUE);
    gtk_misc_set_alignment (GTK_MISC (acceptlab), 0, 0.5);
    
    delaylab = gtk_label_new (_("Retry delay (seconds):"));
    gtk_widget_ref (delaylab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "delaylab", delaylab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (delaylab);
    gtk_table_attach (GTK_TABLE (securitytable), delaylab, 0, 1, 2, 3,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify (GTK_LABEL (delaylab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (delaylab), 0, 0.5);
    
    allowroot = gtk_check_button_new_with_label (_("Allow root login"));
    gtk_widget_ref (allowroot);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "allowroot", allowroot,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (allowroot);
    gtk_table_attach (GTK_TABLE (securitytable), allowroot, 0, 1, 0, 1,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (allowroot), TRUE);
    
    verboseauth = gtk_check_button_new_with_label (_("Verbose authentication errors"));
    gtk_widget_ref (verboseauth);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "verboseauth", verboseauth,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (verboseauth);
    gtk_table_attach (GTK_TABLE (securitytable), verboseauth, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (verboseauth), TRUE);
    
    acceptmenu = gtk_option_menu_new ();
    gtk_widget_ref (acceptmenu);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "acceptmenu", acceptmenu,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (acceptmenu);
    gtk_table_attach (GTK_TABLE (securitytable), acceptmenu, 1, 2, 3, 4,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    acceptmenu_menu = gtk_menu_new ();
    menuitem = gtk_menu_item_new_with_label (_("User"));
    gtk_widget_show (menuitem);
    gtk_menu_append (GTK_MENU (acceptmenu_menu), menuitem);
    menuitem = gtk_menu_item_new_with_label (_("User and group"));
    gtk_widget_show (menuitem);
    gtk_menu_append (GTK_MENU (acceptmenu_menu), menuitem);
    menuitem = gtk_menu_item_new_with_label (_("User, group and others"));
    gtk_widget_show (menuitem);
    gtk_menu_append (GTK_MENU (acceptmenu_menu), menuitem);
    gtk_option_menu_set_menu (GTK_OPTION_MENU (acceptmenu), acceptmenu_menu);
        
    serverpage = gtk_frame_new (_("Local X Servers"));
    gtk_widget_ref (serverpage);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "serverpage", serverpage,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (serverpage);
    gtk_container_set_border_width (GTK_CONTAINER (serverpage), 10);

    gnome_property_box_append_page (GNOME_PROPERTY_BOX (pbox), serverpage,
				    gtk_label_new (_("X Servers")));
    
    servertable = gtk_table_new (2, 1, FALSE);
    gtk_widget_ref (servertable);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "servertable", servertable,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (servertable);
    gtk_container_add (GTK_CONTAINER (serverpage), servertable);
    gtk_container_set_border_width (GTK_CONTAINER (servertable), 10);
    gtk_table_set_row_spacings (GTK_TABLE (servertable), 10);
    gtk_table_set_col_spacings (GTK_TABLE (servertable), 10);
    
    serverlist = gtk_clist_new (2);
    gtk_widget_ref (serverlist);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "serverlist", serverlist,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (serverlist);
    gtk_table_attach (GTK_TABLE (servertable), serverlist, 0, 1, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
    gtk_clist_set_column_width (GTK_CLIST (serverlist), 0, 80);
    gtk_clist_set_column_width (GTK_CLIST (serverlist), 1, 80);
    gtk_clist_column_titles_show (GTK_CLIST (serverlist));
    
    slnumlab = gtk_label_new (_("Number"));
    gtk_widget_ref (slnumlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "slnumlab", slnumlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (slnumlab);
    gtk_clist_set_column_widget (GTK_CLIST (serverlist), 0, slnumlab);
    
    slcmdlab = gtk_label_new (_("Command"));
    gtk_widget_ref (slcmdlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "slcmdlab", slcmdlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (slcmdlab);
    gtk_clist_set_column_widget (GTK_CLIST (serverlist), 1, slcmdlab);
    gtk_label_set_justify (GTK_LABEL (slcmdlab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (slcmdlab), 0, 0.5);
    
    servbbox = gtk_hbutton_box_new ();
    gtk_widget_ref (servbbox);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "servbbox", servbbox,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (servbbox);
    gtk_table_attach (GTK_TABLE (servertable), servbbox, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 0);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (servbbox), GTK_BUTTONBOX_SPREAD);
    
    servadd = gtk_button_new_with_label (_("Add"));
    gtk_widget_ref (servadd);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "servadd", servadd,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (servadd);
    gtk_container_add (GTK_CONTAINER (servbbox), servadd);
    GTK_WIDGET_SET_FLAGS (servadd, GTK_CAN_DEFAULT);
    
    servdel = gtk_button_new_with_label (_("Delete"));
    gtk_widget_ref (servdel);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "servdel", servdel,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (servdel);
    gtk_container_add (GTK_CONTAINER (servbbox), servdel);
    GTK_WIDGET_SET_FLAGS (servdel, GTK_CAN_DEFAULT);
    
    servact = gtk_button_new_with_label (_("Activate"));
    gtk_widget_ref (servact);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "servact", servact,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (servact);
    gtk_container_add (GTK_CONTAINER (servbbox), servact);
    GTK_WIDGET_SET_FLAGS (servact, GTK_CAN_DEFAULT);
    
    xdmcppage = gtk_vbox_new (FALSE, 10);
    gtk_widget_ref (xdmcppage);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "xdmcppage", xdmcppage,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (xdmcppage);
    gtk_container_set_border_width (GTK_CONTAINER (xdmcppage), 10);

    gnome_property_box_append_page (GNOME_PROPERTY_BOX (pbox), xdmcppage,
				    gtk_label_new (_("XDMCP")));
    
    xdmcp = gtk_check_button_new_with_label (_("Enable XDMCP (Remote display management)"));
    gtk_widget_ref (xdmcp);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "xdmcp", xdmcp,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (xdmcp);
    gtk_box_pack_start (GTK_BOX (xdmcppage), xdmcp, FALSE, FALSE, 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (xdmcp), TRUE);
    
    xdmcptable = gtk_table_new (1, 2, FALSE);
    gtk_widget_ref (xdmcptable);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "xdmcptable", xdmcptable,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (xdmcptable);
    gtk_box_pack_start (GTK_BOX (xdmcppage), xdmcptable, TRUE, TRUE, 0);
    gtk_table_set_row_spacings (GTK_TABLE (xdmcptable), 10);
    gtk_table_set_col_spacings (GTK_TABLE (xdmcptable), 10);
    
    protoframe = gtk_frame_new (_("Protocol options"));
    gtk_widget_ref (protoframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "protoframe", protoframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (protoframe);
    gtk_table_attach (GTK_TABLE (xdmcptable), protoframe, 0, 1, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
    
    prototable = gtk_table_new (4, 2, FALSE);
    gtk_widget_ref (prototable);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "prototable", prototable,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (prototable);
    gtk_container_add (GTK_CONTAINER (protoframe), prototable);
    gtk_container_set_border_width (GTK_CONTAINER (prototable), 10);
    gtk_table_set_row_spacings (GTK_TABLE (prototable), 10);
    gtk_table_set_col_spacings (GTK_TABLE (prototable), 10);
    
    maxpentry_adj = gtk_adjustment_new (4, 0, 100, 1, 10, 10);
    maxpentry = gtk_spin_button_new (GTK_ADJUSTMENT (maxpentry_adj), 1, 0);
    gtk_widget_ref (maxpentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxpentry", maxpentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxpentry);
    gtk_table_attach (GTK_TABLE (prototable), maxpentry, 1, 2, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    
    maxmanagentry_adj = gtk_adjustment_new (30, 0, 600, 1, 10, 10);
    maxmanagentry = gtk_spin_button_new (GTK_ADJUSTMENT (maxmanagentry_adj), 1, 0);
    gtk_widget_ref (maxmanagentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxmanagentry", maxmanagentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxmanagentry);
    gtk_table_attach (GTK_TABLE (prototable), maxmanagentry, 1, 2, 1, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    
    maxrentry_adj = gtk_adjustment_new (10, 1, 100, 1, 10, 10);
    maxrentry = gtk_spin_button_new (GTK_ADJUSTMENT (maxrentry_adj), 1, 0);
    gtk_widget_ref (maxrentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxrentry", maxrentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxrentry);
    gtk_table_attach (GTK_TABLE (prototable), maxrentry, 1, 2, 2, 3,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    
    udpentry = gtk_entry_new ();
    gtk_widget_ref (udpentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "udpentry", udpentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (udpentry);
    gtk_table_attach (GTK_TABLE (prototable), udpentry, 1, 2, 3, 4,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_entry_set_text (GTK_ENTRY (udpentry), _("177"));
    
    maxplab = gtk_label_new (_("Max. pending displays:"));
    gtk_widget_ref (maxplab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxplab", maxplab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxplab);
    gtk_table_attach (GTK_TABLE (prototable), maxplab, 0, 1, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify (GTK_LABEL (maxplab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (maxplab), 0, 0.5);
    
    maxmanlab = gtk_label_new (_("Max. MANAGE wait (seconds):"));
    gtk_widget_ref (maxmanlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxmanlab", maxmanlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxmanlab);
    gtk_table_attach (GTK_TABLE (prototable), maxmanlab, 0, 1, 1, 2,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_misc_set_alignment (GTK_MISC (maxmanlab), 0, 0.5);
    
    maxrlab = gtk_label_new (_("Max. remote sessions:"));
    gtk_widget_ref (maxrlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "maxrlab", maxrlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (maxrlab);
    gtk_table_attach (GTK_TABLE (prototable), maxrlab, 0, 1, 2, 3,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify (GTK_LABEL (maxrlab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (maxrlab), 0, 0.5);
    
    udplab = gtk_label_new (_("UDP port number:"));
    gtk_widget_ref (udplab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "udplab", udplab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (udplab);
    gtk_table_attach (GTK_TABLE (prototable), udplab, 0, 1, 3, 4,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (0), 0, 0);
    gtk_label_set_justify (GTK_LABEL (udplab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (udplab), 0, 0.5);
    
    chooserframe = gtk_frame_new (_("Chooser"));
    gtk_widget_ref (chooserframe);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "chooserframe", chooserframe,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (chooserframe);
    gtk_table_attach (GTK_TABLE (xdmcptable), chooserframe, 1, 2, 0, 1,
		      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		      (GtkAttachOptions) (GTK_FILL), 0, 0);
    
    chooserbox = gtk_vbox_new (FALSE, 10);
    gtk_widget_ref (chooserbox);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "chooserbox", chooserbox,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (chooserbox);
    gtk_container_add (GTK_CONTAINER (chooserframe), chooserbox);
    gtk_container_set_border_width (GTK_CONTAINER (chooserbox), 10);
    
    hostimglab = gtk_label_new (_("Host image directory:"));
    gtk_widget_ref (hostimglab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "hostimglab", hostimglab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (hostimglab);
    gtk_box_pack_start (GTK_BOX (chooserbox), hostimglab, FALSE, FALSE, 0);
    gtk_label_set_justify (GTK_LABEL (hostimglab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (hostimglab), 0, 0.5);
    
    hostimgfentry = gnome_file_entry_new (NULL, NULL);
    gtk_widget_ref (hostimgfentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "hostimgfentry", hostimgfentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (hostimgfentry);
    gtk_box_pack_start (GTK_BOX (chooserbox), hostimgfentry, FALSE, FALSE, 0);
    
    defhostlab = gtk_label_new (_("Default host image:"));
    gtk_widget_ref (defhostlab);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "defhostlab", defhostlab,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (defhostlab);
    gtk_box_pack_start (GTK_BOX (chooserbox), defhostlab, FALSE, FALSE, 0);
    gtk_label_set_justify (GTK_LABEL (defhostlab), GTK_JUSTIFY_LEFT);
    gtk_misc_set_alignment (GTK_MISC (defhostlab), 0, 0.5);
    
    defhostpentry = gnome_pixmap_entry_new (NULL, NULL, TRUE);
    gtk_widget_ref (defhostpentry);
    gtk_object_set_data_full (GTK_OBJECT (pbox), "defhostpentry", defhostpentry,
			      (GtkDestroyNotify) gtk_widget_unref);
    gtk_widget_show (defhostpentry);
    gtk_box_pack_start (GTK_BOX (chooserbox), defhostpentry, TRUE, TRUE, 0);
        
    gtk_widget_show_all (pbox);
}   


int main (int argc, gchar* argv[])
{
    static GnomeHelpMenuEntry help = { NULL, "gdmconfig" };
    
    gnome_do_not_create_directories = TRUE;
    gnome_init ("gdmconfig", VERSION, argc, argv);
    gnome_sound_shutdown ();
    help.name = gnome_app_id;
    
    gdm_config_gui_init ();
    gdm_config_parse ();
    gtk_main ();

    exit (EXIT_SUCCESS);
}

/* EOF */
