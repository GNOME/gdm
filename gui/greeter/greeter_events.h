#ifndef GREETER_EVENTS_H
#define GREETER_EVENTS_H

typedef void  (*ActionFunc) (GreeterItemInfo *info,
			     gpointer         user_data);

gint greeter_item_event_handler            (GnomeCanvasItem *item,
					    GdkEvent        *event,
					    gpointer         data);

void greeter_item_register_action_callback (char            *id,
					    ActionFunc       func,
					    gpointer         user_data);

void greeter_item_run_action_callback (const char *id);

#endif /* GREETER_EVENTS_H */
