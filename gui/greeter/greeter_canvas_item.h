#ifndef GREETER_CANVAS_ITEM_H
#define GREETER_CANVAS_ITEM_H

void greeter_item_create_canvas_item (GreeterItemInfo *item);

void greeter_canvas_item_break_set_string (GreeterItemInfo *info,
					   const char *orig,
					   gboolean markup,
					   int max_width,
					   int *width,
					   int *height,
					   GnomeCanvas *canvas,
					   GnomeCanvasItem *real_item);

#endif /* GREETER_CANVAS_ITEM_H */
