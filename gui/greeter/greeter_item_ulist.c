#include "config.h"

#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgnome/libgnome.h>
#include <librsvg/rsvg.h>
#include "vicious.h"

#include "gdm.h"
#include "greeter.h"
#include "greeter_item_ulist.h"
#include "greeter_parser.h"
#include "greeter_configuration.h"

static GList *users = NULL;
static GdkPixbuf *defface;
static gint maxwidth = 0;
static gint maxheight = 0;
static gint number_of_users = 0;
static GtkWidget *pam_entry = NULL;
static GtkWidget *user_list = NULL;

static gboolean selecting_user = FALSE;

typedef struct _GdmGreeterUser GdmGreeterUser;
struct _GdmGreeterUser {
  uid_t uid;
  gchar *login;
  gchar *gecos;
  gchar *homedir;
  GdkPixbuf *picture;
};


enum
{
  GREETER_ULIST_ICON_COLUMN = 0,
  GREETER_ULIST_LABEL_COLUMN,
  GREETER_ULIST_LOGIN_COLUMN
};

static GdmGreeterUser * 
gdm_greeter_user_alloc (const gchar *logname, uid_t uid, const gchar *homedir,
			const char *gecos)
{
	GdmGreeterUser *user;
	GdkPixbuf *img = NULL;
	gchar buf[PIPE_SIZE];
	size_t size;
	int bufsize;
	char *p;

	user = g_new0 (GdmGreeterUser, 1);

	user->uid = uid;
	user->login = g_strdup (logname);
	if (!g_utf8_validate (gecos, -1, NULL))
		user->gecos = ve_locale_to_utf8 (gecos);
	else
		user->gecos = g_strdup (gecos);

	/* Cut up to first comma since those are ugly arguments and
	 * not the name anymore, but only if more then 1 comma is found,
	 * since otherwise it might be part of the actual comment,
	 * this is sort of "heurestic" because there seems to be no
	 * real standard, it's all optional */
	p = strchr (user->gecos, ',');
	if (p != NULL) {
		if (strchr (p+1, ',') != NULL)
			*p = '\0';
	}

	user->homedir = g_strdup (homedir);
	if (defface != NULL)
		user->picture = (GdkPixbuf *)g_object_ref (G_OBJECT (defface));

	if (ve_string_empty (logname))
		return user;

	/* don't read faces, since that requires the daemon */
	if (DOING_GDM_DEVELOPMENT)
		return user;

	/* read initial request */
	do {
		while (read (STDIN_FILENO, buf, 1) == 1)
			if (buf[0] == STX)
				break;
		size = read (STDIN_FILENO, buf, sizeof (buf));
		if (size <= 0)
			return user;
	} while (buf[0] != GDM_NEEDPIC);

	printf ("%c%s\n", STX, logname);
	fflush (stdout);

	do {
		while (read (STDIN_FILENO, buf, 1) == 1)
			if (buf[0] == STX)
				break;
		size = read (STDIN_FILENO, buf, sizeof (buf));
		if (size <= 0)
			return user;
	} while (buf[0] != GDM_READPIC);

	/* both nul terminate and wipe the trailing \n */
	buf[size-1] = '\0';

	if (size < 2) {
		img = NULL;
	} else if (sscanf (&buf[1], "buffer:%d", &bufsize) == 1) {
		char buffer[2048];
		int pos = 0;
		int n;
		GdkPixbufLoader *loader;
		/* we trust the daemon, even if it wanted to give us
		 * bogus bufsize */
		/* the daemon will now print the buffer */
		printf ("%cOK\n", STX);
		fflush (stdout);

		while (read (STDIN_FILENO, buf, 1) == 1)
			if (buf[0] == STX)
				break;

		loader = gdk_pixbuf_loader_new ();

		while ((n = read (STDIN_FILENO, buffer,
				  MIN (sizeof (buffer), bufsize-pos))) > 0) {
			gdk_pixbuf_loader_write (loader, buffer, n, NULL);
			pos += n;
			if (pos >= bufsize)
			       break;	
		}

		gdk_pixbuf_loader_close (loader, NULL);

		img = gdk_pixbuf_loader_get_pixbuf (loader);
		if (img != NULL)
			g_object_ref (G_OBJECT (img));

		g_object_unref (G_OBJECT (loader));

		/* read the "done" bit, but don't check */
		read (STDIN_FILENO, buf, sizeof (buf));
	} else if (access (&buf[1], R_OK) == 0) {
		img = gdk_pixbuf_new_from_file (&buf[1], NULL);
	} else {
		img = NULL;
	}

	/* the daemon is now free to go on */
	printf ("%c\n", STX);
	fflush (stdout);

	if (img != NULL) {
		gint w, h;

		w = gdk_pixbuf_get_width (img);
		h = gdk_pixbuf_get_height (img);

		if (w > h && w > GdmIconMaxWidth) {
			h = h * ((gfloat) GdmIconMaxWidth/w);
			w = GdmIconMaxWidth;
		} else if (h > GdmIconMaxHeight) {
			w = w * ((gfloat) GdmIconMaxHeight/h);
			h = GdmIconMaxHeight;
		}

		if (user->picture != NULL)
			g_object_unref (G_OBJECT (user->picture));

		maxwidth = MAX (maxwidth, w);
		maxheight = MAX (maxheight, h);
		if (w != gdk_pixbuf_get_width (img) ||
		    h != gdk_pixbuf_get_height (img)) {
			user->picture = gdk_pixbuf_scale_simple
				(img, w, h, GDK_INTERP_BILINEAR);
			g_object_unref (G_OBJECT (img));
		} else {
			user->picture = img;
		}
	}

	return user;
}

static gboolean
gdm_greeter_check_shell (const gchar *usersh)
{
    gint found = 0;
    gchar *csh;

    if(!usersh) return FALSE;

    setusershell ();

    while ((csh = getusershell ()) != NULL)
	if (! strcmp (csh, usersh))
	    found = 1;

    endusershell ();

    return (found);
}

static gboolean
gdm_greeter_check_exclude (struct passwd *pwent)
{
	const char * const lockout_passes[] = { "!!", NULL };
	gint i;

	if ( ! GdmAllowRoot && pwent->pw_uid == 0)
		return TRUE;

	if ( ! GdmAllowRemoteRoot && ! GDM_IS_LOCAL && pwent->pw_uid == 0)
		return TRUE;

	if (pwent->pw_uid < GdmMinimalUID)
		return TRUE;

	for (i=0 ; lockout_passes[i] != NULL ; i++)  {
		if (strcmp (lockout_passes[i], pwent->pw_passwd) == 0) {
			return TRUE;
		}
	}

	if (GdmExclude != NULL &&
	    GdmExclude[0] != '\0') {
		char **excludes;
		excludes = g_strsplit (GdmExclude, ",", 0);

		for (i=0 ; excludes[i] != NULL ; i++)  {
			g_strstrip (excludes[i]);
			if (g_ascii_strcasecmp (excludes[i],
						pwent->pw_name) == 0) {
				g_strfreev (excludes);
				return TRUE;
			}
		}
		g_strfreev (excludes);
	}

	return FALSE;
}

static gint 
gdm_greeter_sort_func (gpointer d1, gpointer d2)
{
    GdmGreeterUser *a = d1;
    GdmGreeterUser *b = d2;

    if (!d1 || !d2)
	return (0);

    return (strcmp (a->login, b->login));
}

static void 
gdm_greeter_users_init (void)
{
    GdmGreeterUser *user;
    struct passwd *pwent;
    time_t time_started;

    if (access (GdmDefaultFace, R_OK) == 0) {
		GdkPixbuf *img;
		guint w, h;
	
        img = gdk_pixbuf_new_from_file (GdmDefaultFace, NULL);

		w = gdk_pixbuf_get_width (img);
		h = gdk_pixbuf_get_height (img);

		if (w > h && w > GdmIconMaxWidth) {
			h = h * ((gfloat) GdmIconMaxWidth/w);
			w = GdmIconMaxWidth;
		} else if (h > GdmIconMaxHeight) {
			w = w * ((gfloat) GdmIconMaxHeight/h);
			h = GdmIconMaxHeight;
		}

		maxwidth = MAX (maxwidth, w);
		maxheight = MAX (maxheight, h);

		if (w != gdk_pixbuf_get_width (img) ||
		    h != gdk_pixbuf_get_height (img)) {
			defface = gdk_pixbuf_scale_simple
				(img, w, h, GDK_INTERP_BILINEAR);
			g_object_unref (G_OBJECT (img));

		} else {
			defface = img;
		}
    } else  {
	    syslog (LOG_WARNING,
		    _("Can't open DefaultImage: %s!"),
		    GdmDefaultFace);
    }

    time_started = time (NULL);

    setpwent ();

    pwent = getpwent();
	
    while (pwent != NULL) {

	/* FIXME: fix properly, see bug #111830 */
	if (number_of_users > 500 ||
	    time_started + 5 <= time (NULL)) {
	    user = gdm_greeter_user_alloc ("",
					   9999 /*fake uid*/,
					   "/",
					   _("Too many users to list here..."));
	    users = g_list_insert_sorted (users, user,
					  (GCompareFunc) gdm_greeter_sort_func);
	    /* don't update the size numbers */
	    break;
	}
	
	if (pwent->pw_shell && 
	    gdm_greeter_check_shell (pwent->pw_shell) &&
	    !gdm_greeter_check_exclude(pwent)) {

	    user = gdm_greeter_user_alloc (pwent->pw_name,
					   pwent->pw_uid,
					   pwent->pw_dir,
					   ve_sure_string (pwent->pw_gecos));

	    if ((user) && (! g_list_find_custom (users, user,
				(GCompareFunc) gdm_greeter_sort_func))) {
		users = g_list_insert_sorted(users, user,
					     (GCompareFunc) gdm_greeter_sort_func);
		number_of_users ++;
	    }
	}
	
	pwent = getpwent();
    }

    endpwent ();
}

static void
greeter_populate_user_list (GtkTreeModel *tm)
{
  GList *li;

  for (li = users; li != NULL; li = li->next)
    {
      GdmGreeterUser *usr = li->data;
      GtkTreeIter iter = {0};
      char *label;
      char *login, *gecos;

      login = g_markup_escape_text (usr->login, -1);
      gecos = g_markup_escape_text (usr->gecos, -1);

      label = g_strdup_printf ("<b>%s</b>\n%s",
			       login,
			       gecos);

      g_free (login);
      g_free (gecos);
      gtk_list_store_append (GTK_LIST_STORE (tm), &iter);
      gtk_list_store_set (GTK_LIST_STORE (tm), &iter,
			  GREETER_ULIST_ICON_COLUMN, usr->picture,
			  GREETER_ULIST_LOGIN_COLUMN, usr->login,
			  GREETER_ULIST_LABEL_COLUMN, label,
			  -1);
      g_free (label);
    }
}

static void
user_selected (GtkTreeSelection *selection, gpointer data)
{
  GtkTreeModel *tm = NULL;
  GtkTreeIter iter = {0};

  if (pam_entry == NULL ||
      ! selecting_user)
    return;

  if (gtk_tree_selection_get_selected (selection, &tm, &iter))
    {
      char *login;
      gtk_tree_model_get (tm, &iter, GREETER_ULIST_LOGIN_COLUMN,
			  &login, -1);
      if (login != NULL)
        {
          GreeterItemInfo *pamlabel;
	  if (greeter_probably_login_prompt)
		  gtk_entry_set_text (GTK_ENTRY (pam_entry), login);
	  pamlabel = greeter_lookup_id ("pam-message");
	  if (pamlabel != NULL)
	    g_object_set (G_OBJECT (pamlabel->item),
			  "text", _("Doubleclick on the user\nto log in"),
			  NULL);
	}
    }
}

static void
row_activated (GtkTreeView *tree_view,
	       GtkTreePath *path,
	       GtkTreeViewColumn *column)
{
  GtkTreeModel *tm = NULL;
  GtkTreeIter iter = {0};
  GtkTreeSelection *selection;

  if ( ! selecting_user)
    return;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));

  if (gtk_tree_selection_get_selected (selection, &tm, &iter))
    {
      char *login;
      gtk_tree_model_get (tm, &iter, GREETER_ULIST_LOGIN_COLUMN,
			  &login, -1);
      if (login != NULL)
        {
	  printf ("%c%c%c%s\n", STX, BEL, GDM_INTERRUPT_SELECT_USER,
		  login);
	  fflush (stdout);
	}
    }
}

static void
greeter_generate_userlist (GtkWidget *tv)
{
  GtkTreeModel *tm;
  GtkTreeViewColumn *column;
  GtkTreeSelection *selection;

  gdm_greeter_users_init ();
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tv),
				     FALSE);
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
  if (users != NULL)
    {
      g_signal_connect (selection, "changed",
			G_CALLBACK (user_selected),
			NULL);
      g_signal_connect (tv, "row_activated",
			G_CALLBACK (row_activated),
			NULL);

      tm = (GtkTreeModel *)gtk_list_store_new (3,
					       GDK_TYPE_PIXBUF,
					       G_TYPE_STRING,
					       G_TYPE_STRING);
      gtk_tree_view_set_model (GTK_TREE_VIEW (tv), tm);
      column = gtk_tree_view_column_new_with_attributes
		             (_("Icon"),
			      gtk_cell_renderer_pixbuf_new (),
			      "pixbuf", GREETER_ULIST_ICON_COLUMN,
			      NULL);
      gtk_tree_view_append_column (GTK_TREE_VIEW (tv), column);
      
      column = gtk_tree_view_column_new_with_attributes
		             (_("Username"),
			      gtk_cell_renderer_text_new (),
			      "markup", GREETER_ULIST_LABEL_COLUMN,
			      NULL);
      gtk_tree_view_append_column (GTK_TREE_VIEW (tv), column);
      
      greeter_populate_user_list (tm);
    }
}

gboolean
greeter_item_ulist_setup (void)
{
  GreeterItemInfo *info;

  info = greeter_lookup_id ("user-pw-entry");
  if (info && info->item &&
      GNOME_IS_CANVAS_WIDGET (info->item) &&
      GTK_IS_ENTRY (GNOME_CANVAS_WIDGET (info->item)->widget))
    {
      pam_entry = GNOME_CANVAS_WIDGET (info->item)->widget;
    }
  info = greeter_lookup_id ("userlist");
  if (info && info->item &&
      GNOME_IS_CANVAS_WIDGET (info->item))
    {
      GtkWidget *sw = GNOME_CANVAS_WIDGET (info->item)->widget;
      if (GTK_IS_SCROLLED_WINDOW (sw) && 
	  GTK_IS_TREE_VIEW (GTK_BIN (sw)->child))
        {
          user_list = GTK_BIN (sw)->child;
          greeter_generate_userlist (user_list);
	  if ( ! DOING_GDM_DEVELOPMENT)
            greeter_item_ulist_disable ();
        }
    }
  return TRUE;
}

void
greeter_item_ulist_enable (void)
{
  selecting_user = TRUE;
  if (user_list != NULL)
    gtk_widget_set_sensitive (user_list, TRUE);
}

void
greeter_item_ulist_disable (void)
{
  selecting_user = FALSE;
  if (user_list != NULL)
    gtk_widget_set_sensitive (user_list, FALSE);
}

void
greeter_item_ulist_set_user (const char *user)
{
  gboolean old_selecting_user = selecting_user;
  GtkTreeSelection *selection;
  GtkTreeIter iter = {0};
  GtkTreeModel *tm = NULL;

  if (user_list == NULL)
    return;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (user_list));
  gtk_tree_selection_unselect_all (selection);

  if (ve_string_empty (user))
    return;

  /* Make sure we don't set the pam_entry and pam label stuff,
     this is programatic selection, not user selection */
  selecting_user = FALSE;

  tm = gtk_tree_view_get_model (GTK_TREE_VIEW (user_list));

  if (gtk_tree_model_get_iter_first (tm, &iter))
    {
      do
        {
          char *login;
	  gtk_tree_model_get (tm, &iter, GREETER_ULIST_LOGIN_COLUMN,
			      &login, -1);
	  if (login != NULL && strcmp (user, login) == 0)
	    {
	      GtkTreePath *path = gtk_tree_model_get_path (tm, &iter);
	      gtk_tree_selection_select_iter (selection, &iter);
	      gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (user_list),
					    path, NULL,
					    FALSE, 0.0, 0.0);
	      gtk_tree_path_free (path);
	      break;
	    }
	  
        }
      while (gtk_tree_model_iter_next (tm, &iter));
    }
  selecting_user = old_selecting_user;
}
