#ifndef GREETER_CANVAS_ITEM_H
#define GREETER_CANVAS_ITEM_H

void greeter_item_create_canvas_item (GreeterItemInfo *item);

void greeter_item_recreate_label (GreeterItemInfo *item, const char *text, gboolean markup);

#endif /* GREETER_CANVAS_ITEM_H */
