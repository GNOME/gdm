#ifndef __GREETER_H__
#define __GREETER_H__

#include <gtk/gtk.h>

extern gboolean DOING_GDM_DEVELOPMENT;
extern GtkWidget *canvas;
extern GtkWidget *window;

extern gboolean greeter_probably_login_prompt;

#endif
