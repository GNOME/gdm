#ifndef __GREETER_H__
#define __GREETER_H__

#include <gtk/gtk.h>

extern gboolean DOING_GDM_DEVELOPMENT;
extern GtkWidget *canvas;
extern GtkWidget *window;

void     greeter_abort   (const gchar *format, ...);
gboolean greeter_query   (const gchar *msg);
void     greeter_message (const gchar *msg);

#endif
