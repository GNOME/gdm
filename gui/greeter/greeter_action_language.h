#ifndef __GREETER_ACTION_LANGUAGE_H__
#define __GREETER_ACTION_LANGUAGE_H__

#include "greeter_item.h"

void      greeter_language_init              (void);
gboolean  greeter_language_get_save_language (void);
gchar    *greeter_language_get_language      (const char      *old_language);
void      greeter_action_language            (GreeterItemInfo *info,
					      gpointer         user_data);


#endif

