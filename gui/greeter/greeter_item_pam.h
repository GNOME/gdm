#ifndef __GREETER_ITEM_PAM_H__
#define __GREETER_ITEM_PAM_H__

#include "greeter_item.h"

gboolean greeter_item_pam_setup (void);
void greeter_item_pam_prompt (const char *message,
			      int         entry_len,
			      gboolean    entry_visible);
void greeter_item_pam_message (const char *message);
void greeter_item_pam_error (const char *message);

#endif
