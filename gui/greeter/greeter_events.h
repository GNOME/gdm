#ifndef __GREETER_EVENTS_H__
#define __GREETER_EVENTS_H__

typedef void  (*ActionFunc) (GreeterItemInfo *info,
			     gpointer         user_data);

gint greeter_item_event_handler            (GnomeCanvasItem *item,
					    GdkEvent        *event,
					    gpointer         data);

void greeter_item_register_action_callback (char            *id,
					    ActionFunc       func,
					    gpointer         user_data);

				       


#endif /* __GREETER_EVENTS_H__ */
