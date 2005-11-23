#include <string.h>

#include "greeter_canvas_text.h"

#include <libgnomecanvas/gnome-canvas.h>
#include <libgnomecanvas/gnome-canvas-text.h>
#include <pango/pangoft2.h>


enum 
{
  PROP_0,
  PROP_TEXT,
  PROP_MARKUP
};

static void greeter_canvas_text_class_init (GreeterCanvasTextClass *class);
static void greeter_canvas_text_init (GreeterCanvasText *text);
static void greeter_canvas_text_set_property (GObject            *object,
                                              guint               param_id,
                                              const GValue       *value,
                                              GParamSpec         *pspec);
static void greeter_canvas_text_get_property (GObject            *object,
                                              guint               param_id,
                                              GValue             *value,
                                              GParamSpec         *pspec);

G_DEFINE_TYPE (GreeterCanvasText, greeter_canvas_text, GNOME_TYPE_CANVAS_TEXT)

static void
greeter_canvas_text_class_init (GreeterCanvasTextClass *greeter_class)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (greeter_class);

  gobject_class->set_property = greeter_canvas_text_set_property;
  gobject_class->get_property = greeter_canvas_text_get_property;

  g_object_class_install_property
      (gobject_class,
       PROP_TEXT,
       g_param_spec_string ("text",
                            "Text",
                            "Text to render",
                            NULL,
                            (G_PARAM_READABLE | G_PARAM_WRITABLE)));

  g_object_class_install_property
      (gobject_class,
       PROP_MARKUP,
       g_param_spec_string ("markup",
                            "Markup",
                            "Markup to render",
                            NULL,
                            (G_PARAM_READABLE | G_PARAM_WRITABLE)));
}

static void
greeter_canvas_text_init (GreeterCanvasText *text)
{
}

static gdouble
greeter_canvas_text_get_screen_dpi (GreeterCanvasText *text)
{
  GdkDisplay *display;
  GdkScreen *screen;
  GdkAtom atom, type;
  gint resources_length;
  gchar *resources, *resource, *end;
  gdouble dpi;
  static const gdouble default_dpi = 96.0;

  atom = gdk_atom_intern ("RESOURCE_MANAGER", TRUE);

  if (atom == 0)
    return default_dpi;

  display = 
      gtk_widget_get_display (GTK_WIDGET (GNOME_CANVAS_ITEM (text)->canvas));

  if (display == NULL)
    return default_dpi;

  screen = 
      gtk_widget_get_screen (GTK_WIDGET (GNOME_CANVAS_ITEM (text)->canvas));

  if (screen == NULL)
    return default_dpi;

  gdk_error_trap_push ();
  if (!gdk_property_get (gdk_screen_get_root_window (screen),
                         atom, GDK_TARGET_STRING,
                         0, G_MAXINT,
                         FALSE,
                         &type, NULL, &resources_length,
                         (guchar **) &resources))
    {
      gdk_error_trap_pop ();
      return default_dpi;
    }

  gdk_display_sync (display);
  gdk_error_trap_pop ();

  if (type != GDK_TARGET_STRING)
    return default_dpi;

  if (resources == NULL)
    return default_dpi;

  resource = strstr (resources, "Xft.dpi:\t");

  if (resource == NULL)
    return default_dpi;

  resource += sizeof ("Xft.dpi:\t") - 1;

  dpi = strtod (resource, &end);

  g_assert (end != NULL);

  if ((end == resource) || (*end != '\n'))
    return default_dpi;

  g_free (resources);

  if (dpi < G_MINDOUBLE)
    return default_dpi;

  return dpi;
}

static void
greeter_canvas_text_init_layout (GreeterCanvasText *greeter_item)
{
  GnomeCanvasItem *item;
  GnomeCanvasText *text;

  PangoContext *gtk_context, *context;
  static PangoFT2FontMap *font_map;
  gdouble dpi;

  item = GNOME_CANVAS_ITEM (greeter_item);
  text = GNOME_CANVAS_TEXT (greeter_item);

  if (text->layout != NULL) 
    return;

  gtk_context = gtk_widget_get_pango_context (GTK_WIDGET (item->canvas));

  if (font_map == NULL)
    {
      font_map = (PangoFT2FontMap *) pango_ft2_font_map_new ();
      dpi = greeter_canvas_text_get_screen_dpi (greeter_item);
      pango_ft2_font_map_set_resolution (font_map, dpi, dpi);
    }

  context = pango_ft2_font_map_create_context (font_map);

  pango_context_set_language (context, 
                              pango_context_get_language (gtk_context));
  pango_context_set_base_dir (context,
                              pango_context_get_base_dir (gtk_context));
  pango_context_set_font_description (context,
                                      pango_context_get_font_description (gtk_context));

  text->layout = pango_layout_new (context);
  g_object_unref (context);
}

static void
greeter_canvas_text_set_property (GObject            *object,
                                  guint               param_id,
                                  const GValue       *value,
                                  GParamSpec         *pspec)
{
  switch (param_id)
    {
    case PROP_TEXT:
      greeter_canvas_text_init_layout (GREETER_CANVAS_TEXT (object));
      g_object_set_property (object, "GnomeCanvasText::text", value);
      break;
    case PROP_MARKUP:
      greeter_canvas_text_init_layout (GREETER_CANVAS_TEXT (object));
      g_object_set_property (object, "GnomeCanvasText::markup", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
greeter_canvas_text_get_property (GObject            *object,
                                  guint               param_id,
                                  GValue             *value,
                                  GParamSpec         *pspec)
{
  switch (param_id)
    {
    case PROP_TEXT:
      g_object_get_property (object, "GnomeCanvasText::text", value);
      break;
    case PROP_MARKUP:
      g_object_get_property (object, "GnomeCanvasText::markup", value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}
