#ifndef __GREETER_SESSION_H__
#define __GREETER_SESSION_H__

void        greeter_session_init       (void);
void        greeter_item_session_setup (void);

char *      greeter_session_lookup     (const char *saved_session);
gboolean    greeter_save_session       (void);

char *      greeter_get_gnome_session  (const char *sess_string);
gboolean    greeter_save_gnome_session (void);

#endif /* __GREETER_SESSION_H__ */
