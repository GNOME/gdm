#ifndef GREETER_ITEM_PAM_H
#define GREETER_ITEM_PAM_H

#include "greeter_item.h"

gboolean greeter_item_pam_setup (void);
void greeter_item_pam_prompt (const char *message,
			      int         entry_len,
			      gboolean    entry_visible);
void greeter_item_pam_message (const char *message);
void greeter_item_pam_error (const char *message);
void greeter_item_pam_set_user (const char *user);
void greeter_item_pam_leftover_messages (void);
void greeter_item_pam_login (GtkEntry *entry, GreeterItemInfo *info);

extern gchar *greeter_current_user;

#endif
