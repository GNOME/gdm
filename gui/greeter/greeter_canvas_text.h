#ifndef GREETER_CANVAS_TEXT_H
#define GREETER_CANVAS_TEXT_H

#include <libgnomecanvas/gnome-canvas.h>
#include <libgnomecanvas/gnome-canvas-text.h>

G_BEGIN_DECLS

#define GREETER_TYPE_CANVAS_TEXT            (greeter_canvas_text_get_type ())
#define GREETER_CANVAS_TEXT(obj)            (GTK_CHECK_CAST ((obj), GREETER_TYPE_CANVAS_TEXT, GreeterCanvasText))
#define GREETER_CANVAS_TEXT_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GREETER_TYPE_CANVAS_TEXT, GreeterCanvasTextClass))
#define GREETER_IS_CANVAS_TEXT(obj)         (GTK_CHECK_TYPE ((obj), GREETER_TYPE_CANVAS_TEXT))
#define GREETER_IS_CANVAS_TEXT_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GREETER_TYPE_CANVAS_TEXT))
#define GREETER_CANVAS_TEXT_GET_CLASS(obj)  (GTK_CHECK_GET_CLASS ((obj), GREETER_TYPE_CANVAS_TEXT, GreeterCanvasTextClass))


typedef struct _GreeterCanvasText GreeterCanvasText;
typedef struct _GreeterCanvasTextClass GreeterCanvasTextClass;

struct _GreeterCanvasText {
	GnomeCanvasText text;
};

struct _GreeterCanvasTextClass {
	GnomeCanvasTextClass parent_class;
};


GType greeter_canvas_text_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif
