/* GDM - The Gnome Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
 *
 * This file Copyright (c) 2001 George Lebl
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
#include <gnome.h>
#include <gdk/gdkx.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#ifdef HAVE_LIBXINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <pwd.h>
#include <unistd.h>
#include <syslog.h>
#include "gdmwm.h"
#include "gdm.h"

typedef struct _GdmWindow GdmWindow;
struct _GdmWindow {
	int x, y;
	Window win;
	Window deco;
	Window shadow;
	gboolean ignore_size_hints; /* for gdm windows */
	gboolean center; /* do centering */
	gboolean recenter; /* do re-centering */
        gboolean takefocus; /* permit take focus */
};

static GList *windows = NULL;
static gboolean focus_new_windows = FALSE;
static int no_focus_login = 0;
static Display *wm_disp = NULL;
static Window wm_root = None;
static Window wm_login_window = None;

static gulong XA_WM_PROTOCOLS = 0;
static gulong XA_WM_STATE = 0;
static gulong XA_WM_TAKE_FOCUS = 0;
static gulong XA_COMPOUND_TEXT = 0;

static int trap_depth = 0;

GdkRectangle *gdm_wm_allscreens = NULL;
int gdm_wm_screens = 0;
GdkRectangle gdm_wm_screen = {0,0,0,0};

void 
gdm_wm_screen_init (int cur_screen_num)
{
#ifdef HAVE_LIBXINERAMA
	gboolean have_xinerama = FALSE;

	gdk_flush ();
	gdk_error_trap_push ();
	have_xinerama = XineramaIsActive (GDK_DISPLAY ());
	gdk_flush ();
	if (gdk_error_trap_pop () != 0)
		have_xinerama = FALSE;

	if (have_xinerama) {
		int screen_num, i;
		XineramaScreenInfo *xscreens =
			XineramaQueryScreens (GDK_DISPLAY (),
					      &screen_num);


		if (screen_num <= 0) {
			/* should NEVER EVER happen */
			syslog (LOG_ERR, "Xinerama active, but <= 0 screens?");
			gdm_wm_screen.x = 0;
			gdm_wm_screen.y = 0;
			gdm_wm_screen.width = gdk_screen_width ();
			gdm_wm_screen.height = gdk_screen_height ();

			gdm_wm_allscreens = g_new0 (GdkRectangle, 1);
			gdm_wm_allscreens[0] = gdm_wm_screen;
			gdm_wm_screens = 1;
			return;
		}

		if (screen_num <= cur_screen_num)
			cur_screen_num = 0;

		gdm_wm_allscreens = g_new0 (GdkRectangle, screen_num);
		gdm_wm_screens = screen_num;

		for (i = 0; i < screen_num; i++) {
			gdm_wm_allscreens[i].x = xscreens[i].x_org;
			gdm_wm_allscreens[i].y = xscreens[i].y_org;
			gdm_wm_allscreens[i].width = xscreens[i].width;
			gdm_wm_allscreens[i].height = xscreens[i].height;

			if (cur_screen_num == i)
				gdm_wm_screen = gdm_wm_allscreens[i];
		}

		XFree (xscreens);
	} else
#endif
	{
		gdm_wm_screen.x = 0;
		gdm_wm_screen.y = 0;
		gdm_wm_screen.width = gdk_screen_width ();
		gdm_wm_screen.height = gdk_screen_height ();

		gdm_wm_allscreens = g_new0 (GdkRectangle, 1);
		gdm_wm_allscreens[0] = gdm_wm_screen;
		gdm_wm_screens = 1;
	}
#if 0
	/* for testing Xinerama support on non-xinerama setups */
	{
		gdm_wm_screen.x = 100;
		gdm_wm_screen.y = 100;
		gdm_wm_screen.width = gdk_screen_width () / 2 - 100;
		gdm_wm_screen.height = gdk_screen_height () / 2 - 100;

		gdm_wm_allscreens = g_new0 (GdkRectangle, 2);
		gdm_wm_allscreens[0] = gdm_wm_screen;
		gdm_wm_allscreens[1].x = gdk_screen_width () / 2;
		gdm_wm_allscreens[1].y = gdk_screen_height () / 2;
		gdm_wm_allscreens[1].width = gdk_screen_width () / 2;
		gdm_wm_allscreens[1].height = gdk_screen_height () / 2;
		gdm_wm_screens = 2;
	}
#endif
}

void 
gdm_wm_set_screen (int cur_screen_num)
{
	if (cur_screen_num >= gdm_wm_screens || cur_screen_num < 0)
		cur_screen_num = 0;

	gdm_wm_screen = gdm_wm_allscreens[cur_screen_num];
}

/* Not really a WM function, center a gtk window by setting uposition */
void
gdm_wm_center_window (GtkWindow *cw) 
{
	GtkRequisition req;
        gint x, y;

	gtk_widget_size_request (GTK_WIDGET (cw), &req);

	x = gdm_wm_screen.x + (gdm_wm_screen.width - req.width)/2;
	y = gdm_wm_screen.y + (gdm_wm_screen.height - req.height)/2;	

	if (x < gdm_wm_screen.x)
		x = gdm_wm_screen.x;
	if (y < gdm_wm_screen.y)
		y = gdm_wm_screen.y;

 	gtk_widget_set_uposition (GTK_WIDGET (cw), x, y);	
}



static void
trap_push (void)
{
	trap_depth ++;
	gdk_error_trap_push ();
}

static gboolean
trap_pop (void)
{
	trap_depth --;
	if (trap_depth <= 0)
		XSync (wm_disp, False);
	return gdk_error_trap_pop ();
}

/* stolen from gwmh */
static gpointer
get_typed_property_data (Display *xdisplay,
			 Window   xwindow,
			 Atom     property,
			 Atom     requested_type,
			 gint    *size_p,
			 guint    expected_format)
{
  static const guint prop_buffer_lengh = 1024 * 1024;
  unsigned char *prop_data = NULL;
  Atom type_returned = 0;
  unsigned long nitems_return = 0, bytes_after_return = 0;
  int format_returned = 0;
  gpointer data = NULL;
  gboolean abort = FALSE;

  g_return_val_if_fail (size_p != NULL, NULL);
  *size_p = 0;

  gdk_error_trap_push ();

  abort = XGetWindowProperty (xdisplay,
			      xwindow,
			      property,
			      0, prop_buffer_lengh,
			      False,
			      requested_type,
			      &type_returned, &format_returned,
			      &nitems_return,
			      &bytes_after_return,
			      &prop_data) != Success;
  if (gdk_error_trap_pop () ||
      type_returned == None)
    abort++;
  if (!abort &&
      requested_type != AnyPropertyType &&
      requested_type != type_returned)
    {
      /* aparently this can happen for some properties of broken apps, be silent */
      abort++;
    }
  if (!abort && bytes_after_return)
    {
      g_warning (G_GNUC_PRETTY_FUNCTION "(): Eeek, property has more than %u bytes, stored on harddisk?",
		 prop_buffer_lengh);
      abort++;
    }
  if (!abort && expected_format && expected_format != format_returned)
    {
      g_warning (G_GNUC_PRETTY_FUNCTION "(): Expected format (%u) unmatched (%d), programmer was drunk?",
		 expected_format, format_returned);
      abort++;
    }
  if (!abort && prop_data && nitems_return && format_returned)
    {
      switch (format_returned)
	{
	case 32:
	  *size_p = nitems_return * 4;
	  if (sizeof (gulong) == 8)
	    {
	      guint32 i, *mem = g_malloc0 (*size_p + 1);
	      gulong *prop_longs = (gulong*) prop_data;

	      for (i = 0; i < *size_p / 4; i++)
		mem[i] = prop_longs[i];
	      data = mem;
	    }
	  break;
	case 16:
	  *size_p = nitems_return * 2;
	  break;
	case 8:
	  *size_p = nitems_return;
	  break;
	default:
	  g_warning ("Unknown property data format with %d bits (extraterrestrial?)",
		     format_returned);
	  break;
	}
      if (!data && *size_p)
	{
	  guint8 *mem = NULL;

	  if (format_returned == 8 && type_returned == XA_COMPOUND_TEXT)
	    {
	      gchar **tlist = NULL;
	      gint count = gdk_text_property_to_text_list (type_returned, 8, prop_data,
							   nitems_return, &tlist);

	      if (count && tlist && tlist[0])
		{
		  mem = (guint8 *)g_strdup (tlist[0]);
		  *size_p = strlen ((char *)mem);
		}
	      if (tlist)
		gdk_free_text_list (tlist);
	    }
	  if (!mem)
	    {
	      mem = g_malloc (*size_p + 1);
	      memcpy (mem, prop_data, *size_p);
	      mem[*size_p] = 0;
	    }
	  data = mem;
	}
    }

  if (prop_data)
    XFree (prop_data);
  
  return data;
}

/* stolen from gwmh */
static gboolean
wm_protocol_check_support (Window xwin,
			   Atom   check_atom)
{
  Atom *pdata = NULL;
  guint32 *gdata = NULL;
  int n_pids = 0;
  gboolean is_supported = FALSE;
  guint i, n_gids = 0;

  trap_push ();

  if (!XGetWMProtocols (wm_disp,
			xwin,
			&pdata,
			&n_pids))
    {
      gint size = 0;

      gdata = get_typed_property_data (wm_disp,
				       xwin,
				       XA_WM_PROTOCOLS,
				       XA_WM_PROTOCOLS,
				       &size, 32);
      n_gids = size / 4;
    }

  trap_pop ();

  for (i = 0; i < n_pids; i++)
    if (pdata[i] == check_atom)
      {
	is_supported = TRUE;
	break;
      }
  if (pdata)
    XFree (pdata);
  if (!is_supported)
    for (i = 0; i < n_gids; i++)
      if (gdata[i] == check_atom)
        {
	  is_supported = TRUE;
	  break;
        }
  g_free (gdata);

  return is_supported;
}

static GList *
find_window_list (Window w, gboolean deco_ok)
{
	GList *li;

	for (li = windows; li != NULL; li = li->next) {
		GdmWindow *gw = li->data;

		if (gw->win == w)
			return li;
		if (deco_ok &&
		    (gw->deco == w ||
		     gw->shadow == w))
			return li;
	}

	return NULL;
}

static GdmWindow *
find_window (Window w, gboolean deco_ok)
{
	GList *li = find_window_list (w, deco_ok);
	if (li == NULL)
		return NULL;
	else
		return li->data;
}

void
gdm_wm_focus_window (Window window)
{
	XWindowAttributes attribs = {0};
	GdmWindow *win;

	if (no_focus_login > 0 &&
	    window == wm_login_window)
		return;

	win = find_window (window, TRUE);
	if (win != NULL &&
	    ! win->takefocus)
		return;

	trap_push ();

	XGetWindowAttributes (wm_disp, window, &attribs);
	if (attribs.map_state == IsUnmapped) {
		trap_pop ();
		return;
	}

	if (wm_protocol_check_support (window, XA_WM_TAKE_FOCUS)) {
		XEvent xevent = { 0, };

		xevent.type = ClientMessage;
		xevent.xclient.window = window;
		xevent.xclient.message_type = XA_WM_PROTOCOLS;
		xevent.xclient.format = 32;
		xevent.xclient.data.l[0] = XA_WM_TAKE_FOCUS;
		xevent.xclient.data.l[1] = CurrentTime;

		XSendEvent (wm_disp, window, False, 0, &xevent);
		XSync (wm_disp, False);
	}

	XSetInputFocus (wm_disp,
			window,
			RevertToPointerRoot,
			CurrentTime);
	trap_pop ();
}

static void
center_x_window (GdmWindow *gw, Window w, Window hintwin)
{
	XSizeHints hints;
	Status status;
	long ret;
	int x, y;
	Window root;
	unsigned int width, height, border, depth;
	gboolean can_resize, can_reposition;

	trap_push ();

	status = XGetWMNormalHints (wm_disp,
				    hintwin,
				    &hints,
				    &ret);

	if ( ! status) {
		trap_pop ();
		return;
	}

	/* allow resizing when PSize is given, just don't allow centering when
	 * PPosition is goven */
	can_resize = ! (hints.flags & USSize);
	can_reposition = ! (hints.flags & USPosition ||
			    hints.flags & PPosition);

	if (can_reposition && ! gw->center)
		can_reposition = FALSE;

	if (gw->ignore_size_hints) {
		can_resize = TRUE;
		can_reposition = TRUE;
	}

	if ( ! can_resize &&
	     ! can_reposition) {
		trap_pop ();
		return;
	}

	XGetGeometry (wm_disp, w,
		      &root, &x, &y, &width, &height, &border, &depth);

	/* we replace the x,y and width,height with some new values */

	if (can_resize) {
		if (width > gdm_wm_screen.width)
			width = gdm_wm_screen.width;
		if (height > gdm_wm_screen.height)
			height = gdm_wm_screen.height;
	}

	if (can_reposition) {
		/* we wipe the X with some new values */
		x = gdm_wm_screen.x + (gdm_wm_screen.width - width)/2;
		y = gdm_wm_screen.y + (gdm_wm_screen.height - height)/2;	

		if (x < gdm_wm_screen.x)
			x = gdm_wm_screen.x;
		if (y < gdm_wm_screen.y)
			y = gdm_wm_screen.y;
	}
	
	XMoveResizeWindow (wm_disp, w, x, y, width, height);

	if (gw->center && ! gw->recenter) {
		gw->center = FALSE;
	}

	trap_pop ();
}

#ifndef MWMUTIL_H_INCLUDED

typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
} MotifWmHints, MwmHints;

#define MWM_HINTS_DECORATIONS   (1L << 1)

#define MWM_DECOR_BORDER        (1L << 1)

#endif /* MWMUTIL_H_INCLUDED */

static gboolean
has_deco (Window win)
{
	static Atom hints_atom = None;
	MotifWmHints *hints;
	Atom type;
	gint format;
	gulong nitems;
	gulong bytes_after;
	gboolean border = TRUE;

	trap_push ();

	if (hints_atom == None)
		hints_atom = XInternAtom (wm_disp, "_MOTIF_WM_HINTS", FALSE);

	hints = NULL;

	XGetWindowProperty (wm_disp, win,
			    hints_atom, 0,
			    sizeof (MotifWmHints) / sizeof (long),
			    False, AnyPropertyType, &type, &format, &nitems,
			    &bytes_after, (guchar **)&hints);

	if (type != None &&
	    hints != NULL &&
	    hints->flags & MWM_HINTS_DECORATIONS &&
	    ! (hints->decorations & MWM_DECOR_BORDER)) {
		border = FALSE;
	}

	if (hints != NULL)
		XFree (hints);

	trap_pop ();

	return border;
}


static void
add_deco (GdmWindow *w)
{
	int x, y;
	Window root;
	unsigned int width, height, border, depth;
	XWindowAttributes attribs = { 0, };
	int black;

	trap_push ();

	if ( ! has_deco (w->win)) {
		trap_pop ();
		return;
	}

	XGetGeometry (wm_disp, w->win,
		      &root, &x, &y, &width, &height, &border, &depth);

	black = BlackPixel (wm_disp, DefaultScreen (wm_disp));

	/* all but the login window has shadows */
	if (w->win != wm_login_window) {
		w->shadow = XCreateSimpleWindow (wm_disp,
						 wm_root,
						 x + 4, y + 4,
						 width + 2 + 2 * border,
						 height + 2 + 2 * border,
						 0, 
						 black, black);

		XMapWindow (wm_disp, w->shadow);
	}

	w->deco = XCreateSimpleWindow (wm_disp,
				       wm_root,
				       x - 1, y - 1,
				       width + 2 + 2 * border,
				       height + 2 + 2 * border,
				       0, 
				       black, black);

	XGetWindowAttributes (wm_disp, w->deco, &attribs);
	XSelectInput (wm_disp, w->deco,
		      attribs.your_event_mask |
		      EnterWindowMask |
		      SubstructureNotifyMask |
		      SubstructureRedirectMask);

	XMapWindow (wm_disp, w->deco);

	XReparentWindow (wm_disp, w->win, w->deco, 1, 1);

	trap_pop ();
}

static gboolean
is_wm_class (XClassHint *hint, const char *string, int len)
{
	if (len > 0) {
		return ((hint->res_name != NULL &&
			 strncmp (hint->res_name, string, len) == 0) ||
			(hint->res_class != NULL &&
			 strncmp (hint->res_class, string, len) == 0));
	} else {
		return ((hint->res_name != NULL &&
			 strcmp (hint->res_name, string) == 0) ||
			(hint->res_class != NULL &&
			 strcmp (hint->res_class, string) == 0));
	}
}

static GdmWindow *
add_window (Window w, gboolean center)
{
	GdmWindow *gw;

	gw = find_window (w, FALSE);
	if (gw == NULL) {
		XClassHint hint = { NULL, NULL };
		XWMHints *wmhints;
		int x, y;
		Window root;
		unsigned int width, height, border, depth;

		gw = g_new0 (GdmWindow, 1);
		gw->win = w;
		windows = g_list_prepend (windows, gw);

		trap_push ();

		/* add "centering" */
		gw->ignore_size_hints = FALSE;
		gw->center = center;
		gw->recenter = FALSE;
		gw->takefocus = TRUE;

		wmhints = XGetWMHints (wm_disp, w);
		if (wmhints != NULL) {
			/* NoInput windows */
			if ((wmhints->flags & InputHint) &&
			    ! wmhints->input) {
				gw->takefocus = FALSE;
			}
			XFree (wmhints);
		}

		/* hack, set USpos/size on login window */
		if (w == wm_login_window) {
			long ret;
			XSizeHints hints;
			XGetWMNormalHints (wm_disp, w, &hints, &ret);
			hints.flags |= USPosition | USSize;
			XSetWMNormalHints (wm_disp, w, &hints);
			gw->center = FALSE;
			gw->recenter = FALSE;
		} else if (XGetClassHint (wm_disp, w, &hint)) {
			if (is_wm_class (&hint, "gdm", 3)) {
				gw->ignore_size_hints = TRUE;
				gw->center = TRUE;
				gw->recenter = TRUE;
			} else if (is_wm_class (&hint, "gkrellm", 0)) {
				/* hack, gkrell is stupid and doesn't set
				 * right hints, such as USPosition and other
				 * such stuff */
				gw->center = FALSE;
				gw->recenter = FALSE;
			} else if (is_wm_class (&hint, "xscribble", 0)) {
				/* hack, xscribble mustn't take focus */
				gw->takefocus = FALSE;
			}
			if (hint.res_name != NULL)
				XFree (hint.res_name);
			if (hint.res_class != NULL)
				XFree (hint.res_class);
		}

		XGetGeometry (wm_disp, w,
			      &root, &x, &y, &width, &height, &border, &depth);

		gw->x = x;
		gw->y = x;

		center_x_window (gw, w, w);
		add_deco (gw);

		XAddToSaveSet (wm_disp, w);

		trap_pop ();
	}
	return gw;
}

static void
remove_window (Window w)
{
	GList *li = find_window_list (w, FALSE);

	if (li != NULL) {
		GdmWindow *gw = li->data;

		li->data = NULL;

		trap_push ();

		XRemoveFromSaveSet (wm_disp, w);

		gw->win = None;

		if (gw->deco != None) {
			XDestroyWindow (wm_disp, gw->deco);
			gw->deco = None;
		}
		if (gw->shadow != None) {
			XDestroyWindow (wm_disp, gw->shadow);
			gw->shadow = None;
		}
		trap_pop ();

		windows = g_list_remove_link (windows, li);
		g_list_free_1 (li);

		g_free (gw);
	}
}

static void
revert_focus_to_login (void)
{
	if (wm_login_window != None) {
		gdm_wm_focus_window (wm_login_window);
	}
}

static void
add_all_current_windows (void)
{
	Window *children = NULL;
	Window xparent, xroot;
	guint size = 0;

	gdk_flush ();
	XSync (wm_disp, False);
	trap_push ();

	XGrabServer (wm_disp);

	if (XQueryTree (wm_disp, 
			wm_root,
			&xroot,
			&xparent,
			&children,
			&size)) {
		int i;

		for (i = 0; i < size; i++) {
			XWindowAttributes attribs = {0};

			XGetWindowAttributes (wm_disp,
					      children[i],
					      &attribs);

			if ( ! attribs.override_redirect &&
			    attribs.map_state != IsUnmapped) {
				add_window (children[i], FALSE /*center*/);
			}
		}

		if (children != NULL)
			XFree (children);
	}

	XUngrabServer (wm_disp);

	trap_pop ();
}

static void
reparent_to_root (GdmWindow *gw)
{
	/* only if reparented */
	if (gw->deco != None) {
		trap_push ();

		XReparentWindow (wm_disp, gw->win, wm_root, gw->x, gw->y);
		XSync (wm_disp, False);

		trap_pop ();
	}
}

static void
shadow_follow (GdmWindow *gw)
{
	int x, y;
	Window root;
	unsigned int width, height, border, depth;

	if (gw->shadow == None)
		return;

	trap_push ();

	XGetGeometry (wm_disp, gw->deco,
		      &root, &x, &y, &width, &height, &border, &depth);

	x += 5;
	y += 5;

	XMoveResizeWindow (wm_disp, gw->shadow, x, y, width, height);

	trap_pop ();
}

static void
event_process (XEvent *ev)
{
	GdmWindow *gw;
	Window w;
	XWindowChanges wchanges;

	trap_push ();

	switch (ev->type) {
	case MapRequest:
		w = ev->xmaprequest.window;
		gw = find_window (w, FALSE);
		if (gw == NULL) {
			if (ev->xmaprequest.parent == wm_root) {
				XGrabServer (wm_disp);
				gw = add_window (w, TRUE /*center*/);
				XUngrabServer (wm_disp);
			}
		}
		XMapWindow (wm_disp, w);
		break;
	case ConfigureRequest:
		XGrabServer (wm_disp);
		w = ev->xconfigurerequest.window;
		gw = find_window (w, FALSE);
		wchanges.border_width = ev->xconfigurerequest.border_width;
		wchanges.sibling = ev->xconfigurerequest.above;
		wchanges.stack_mode = ev->xconfigurerequest.detail;
		if (gw == NULL ||
		    gw->deco == None) {
			wchanges.x = ev->xconfigurerequest.x;
			wchanges.y = ev->xconfigurerequest.y;
		} else {
			wchanges.x = 1;
			wchanges.y = 1;
		}
		wchanges.width = ev->xconfigurerequest.width;
		wchanges.height = ev->xconfigurerequest.height;
		XConfigureWindow (wm_disp,
				  w,
				  ev->xconfigurerequest.value_mask,
				  &wchanges);
		if (gw != NULL) {
			gw->x = ev->xconfigurerequest.x;
			gw->y = ev->xconfigurerequest.y;
			if (gw->deco != None) {
				wchanges.x = ev->xconfigurerequest.x - 1;
				wchanges.y = ev->xconfigurerequest.y - 1;
				wchanges.width = ev->xconfigurerequest.width + 2
					+ 2*ev->xconfigurerequest.border_width;;
				wchanges.height = ev->xconfigurerequest.height + 2
					+ 2*ev->xconfigurerequest.border_width;;
				wchanges.border_width = 0;
				XConfigureWindow (wm_disp,
						  gw->deco,
						  ev->xconfigurerequest.value_mask,
						  &wchanges);
				center_x_window (gw, gw->deco, gw->win);
			} else {
				center_x_window (gw, gw->win, gw->win);
			}
			shadow_follow (gw);
		}
		XUngrabServer (wm_disp);
		break;
	case CirculateRequest:
		w = ev->xcirculaterequest.window;
		gw = find_window (w, FALSE);
		if (gw == NULL) {
			if (ev->xcirculaterequest.place == PlaceOnTop)
				XRaiseWindow (wm_disp, w);
			else
				XLowerWindow (wm_disp, w);
		} else {
			if (ev->xcirculaterequest.place == PlaceOnTop) {
				if (gw->shadow != None)
					XRaiseWindow (wm_disp, gw->shadow);
				if (gw->deco != None)
					XRaiseWindow (wm_disp, gw->deco);
				else
					XRaiseWindow (wm_disp, gw->win);
			} else {
				if (gw->deco != None)
					XLowerWindow (wm_disp, gw->deco);
				else
					XLowerWindow (wm_disp, gw->win);
				if (gw->shadow != None)
					XLowerWindow (wm_disp, gw->shadow);
			}
		}
		break;
	case MapNotify:
		w = ev->xmap.window;
		if ( ! ev->xmap.override_redirect &&
		    focus_new_windows) {
			gw = find_window (w, FALSE);
			if (gw != NULL)
				gdm_wm_focus_window (w);
		}
		break;
	case UnmapNotify:
		w = ev->xunmap.window;
		gw = find_window (w, FALSE);
		if (gw != NULL) {
			XGrabServer (wm_disp);
			if (gw->deco != None)
				XUnmapWindow (wm_disp, gw->deco);
			if (gw->shadow != None)
				XUnmapWindow (wm_disp, gw->shadow);
			reparent_to_root (gw);
			remove_window (w);
			XDeleteProperty (wm_disp, w, XA_WM_STATE);
			if (w != wm_login_window)
				revert_focus_to_login ();
			XUngrabServer (wm_disp);
		}
		break;
	case DestroyNotify:
		w = ev->xdestroywindow.window;
		gw = find_window (w, FALSE);
		if (gw != NULL) {
			XGrabServer (wm_disp);
			remove_window (w);
			if (w != wm_login_window)
				revert_focus_to_login ();
			XUngrabServer (wm_disp);
		}
		break;
	case EnterNotify:
		w = ev->xcrossing.window;
		gw = find_window (w, TRUE);
		if (gw != NULL)
			gdm_wm_focus_window (gw->win);
		break;
	default:
		break;
	}

	trap_pop ();
}

/* following partly stolen from gdk */
static GPollFD event_poll_fd;

static gboolean  
event_prepare (gpointer  source_data, 
	       GTimeVal *current_time,
	       gint     *timeout,
	       gpointer  user_data)
{
	*timeout = -1;
	return XPending (wm_disp);
}

static gboolean  
event_check (gpointer  source_data,
	     GTimeVal *current_time,
	     gpointer  user_data)
{
	if (event_poll_fd.revents & G_IO_IN) {
		return XPending (wm_disp);
	} else {
		return FALSE;
	}
}

static void
process_events (void)
{
	while (XPending (wm_disp)) {
		XEvent ev;
		XNextEvent (wm_disp, &ev);
		event_process (&ev);
	}
}

static gboolean  
event_dispatch (gpointer  source_data,
		GTimeVal *current_time,
		gpointer  user_data)
{
	process_events ();

	return TRUE;
}

static GSourceFuncs event_funcs = {
	event_prepare,
	event_check,
	event_dispatch,
	(GDestroyNotify)g_free
};

/* HAAAAAAAAAAAAAACK */
void
gnome_dock_item_grab_pointer (GnomeDockItem *item)
{
  GdkCursor *fleur;

  fleur = gdk_cursor_new (GDK_FLEUR);

  /* Hm, not sure this is the right thing to do, but it seems to work.  */
  while (gdk_pointer_grab (item->bin_window,
                           FALSE,
                           (GDK_BUTTON1_MOTION_MASK |
                            GDK_POINTER_MOTION_HINT_MASK |
                            GDK_BUTTON_RELEASE_MASK),
                           NULL,
                           fleur,
                           GDK_CURRENT_TIME) != 0)
	  gtk_main_iteration ();

  gdk_cursor_destroy (fleur);
}

void
gdm_wm_init (Window login_window)
{
	XWindowAttributes attribs = { 0, };

	wm_login_window = login_window;

	wm_disp = XOpenDisplay (gdk_display_name);
	if (wm_disp == NULL) {
		/* EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEK! */
		wm_disp = GDK_DISPLAY ();
		return;
	}

	trap_push ();

	XA_WM_PROTOCOLS = XInternAtom (wm_disp, "WM_PROTOCOLS", False);
	XA_WM_STATE = XInternAtom (wm_disp, "WM_STATE", False);
	XA_WM_TAKE_FOCUS = XInternAtom (wm_disp, "WM_TAKE_FOCUS", False);

	XA_COMPOUND_TEXT = gdk_atom_intern ("COMPOUND_TEXT", FALSE);


	wm_root = DefaultRootWindow (wm_disp);

	add_all_current_windows ();

	g_source_add (GDK_PRIORITY_EVENTS, FALSE, &event_funcs, NULL, NULL, NULL);

	event_poll_fd.fd = ConnectionNumber (wm_disp);
	event_poll_fd.events = G_IO_IN;

	g_main_add_poll (&event_poll_fd, GDK_PRIORITY_EVENTS);


	/* set event mask for events on root window */
	XGetWindowAttributes (wm_disp, wm_root, &attribs);
	XSelectInput (wm_disp, wm_root,
		      attribs.your_event_mask |
		      SubstructureNotifyMask |
		      SubstructureRedirectMask);

	trap_pop ();
}

void
gdm_wm_focus_new_windows (gboolean focus)
{
	focus_new_windows = focus;
}

void
gdm_wm_no_login_focus_push (void)
{
	no_focus_login ++;
}

void
gdm_wm_no_login_focus_pop (void)
{
	no_focus_login --;
}

void
gdm_wm_get_window_pos (Window window, int *xp, int *yp)
{
	int x, y;
	Window root;
	unsigned int width, height, border, depth;
	GdmWindow *gw;

	trap_push ();

	gw = find_window (window, TRUE);

	if (gw == NULL) {
		XGetGeometry (wm_disp, window,
			      &root, &x, &y, &width, &height, &border, &depth);

		*xp = x;
		*yp = y;

		trap_pop ();

		return;
	}

	if (gw->deco != None) {
		XGetGeometry (wm_disp, gw->deco,
			      &root, &x, &y, &width, &height, &border, &depth);
		*xp = x + 1;
		*yp = y + 1;
	} else {
		XGetGeometry (wm_disp, gw->win,
			      &root, &x, &y, &width, &height, &border, &depth);
		*xp = x;
		*yp = y;
	}

	trap_pop ();
}

void
gdm_wm_move_window_now (Window window, int x, int y)
{
	GdmWindow *gw;

	trap_push ();

	gw = find_window (window, TRUE);

	if (gw == NULL) {
		XMoveWindow (wm_disp, window, x, y);

		XSync (wm_disp, False);
		trap_pop ();
	}

	if (gw->deco != None)
		XMoveWindow (wm_disp, gw->deco, x - 1, y - 1);
	else
		XMoveWindow (wm_disp, gw->win, x, y);
	if (gw->shadow != None)
		XMoveWindow (wm_disp, gw->deco, x + 4, y + 4);

	XSync (wm_disp, False);
	trap_pop ();
}

/* EOF */
