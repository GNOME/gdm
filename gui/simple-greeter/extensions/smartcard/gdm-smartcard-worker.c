#include "config.h"

#include <fcntl.h>
#include <locale.h>
#include <sys/prctl.h>
#include <stdlib.h>
#include <unistd.h>

#include <glib.h>

#include "gdm-smartcard-manager.h"
#include "gdm-smartcard.h"

#ifndef GDM_SMARTCARDS_CONF
#define GDM_SMARTCARDS_CONF GDMCONFDIR "/smartcards.conf"
#endif

#ifndef GDM_SMARTCARDS_GROUP
#define GDM_SMARTCARDS_GROUP "Smartcards"
#endif

#ifndef GDM_SMARTCARDS_KEY_ENABLED
#define GDM_SMARTCARDS_KEY_ENABLED "Enabled"
#endif

#ifndef GDM_SMARTCARDS_KEY_DRIVER
#define GDM_SMARTCARDS_KEY_DRIVER "Driver"
#endif

static GMainLoop *event_loop;
static GdmSmartcardManager *manager;
static int signal_pipe_fds[2] = { -1, -1 };

static void
on_smartcard_event (const char *event_string)
{
        g_debug ("smartcard event '%s' happened", event_string);
        g_print ("%s", event_string);
        fflush (stdout);
}

static void
watch_for_smartcards (void)
{
        GError *error;
        char *driver;
        GKeyFile *cfg;

        cfg = g_key_file_new ();

        error = NULL;
        driver = NULL;
        if (g_key_file_load_from_file (cfg, GDM_SMARTCARDS_CONF, G_KEY_FILE_NONE, &error)) {
                if (!g_key_file_get_boolean (cfg, GDM_SMARTCARDS_GROUP, GDM_SMARTCARDS_KEY_ENABLED, &error)) {
                        g_debug ("smartcard support is not enabled");
                        goto out;
                }

                driver = g_key_file_get_string (cfg, GDM_SMARTCARDS_GROUP, GDM_SMARTCARDS_KEY_DRIVER, NULL);
                g_debug ("smartcards driver is set to '%s'",
                        driver == NULL || driver[0] == '\0'? "<automatic>" : driver);
        }

        g_debug ("watching for smartcard insertion and removal events");
        manager = gdm_smartcard_manager_new (driver);
        g_free (driver);

        g_signal_connect_swapped (manager,
                                  "smartcard-inserted",
                                  G_CALLBACK (on_smartcard_event),
                                  "I");

        g_signal_connect_swapped (manager,
                                  "smartcard-removed",
                                  G_CALLBACK (on_smartcard_event),
                                  "R");

        error = NULL;
        if (!gdm_smartcard_manager_start (manager, &error)) {
            g_object_unref (manager);
            manager = NULL;

            if (error != NULL) {
                    g_debug ("%s", error->message);
                    g_error_free (error);
            } else {
                    g_debug ("could not start smartcard manager");

            }
            goto out;
        }
out:
        g_key_file_free (cfg);
}

static void
stop_watching_for_smartcards (void)
{
        if (manager != NULL) {
                gdm_smartcard_manager_stop (manager);
                g_object_unref (manager);
                manager = NULL;
        }
}

static void
on_alrm_signal (int signal_number)
{
        raise (SIGKILL);
}

static void
on_term_signal (int signal_number)
{
        close (signal_pipe_fds[1]);
        signal_pipe_fds[1] = -1;

        /* Give us 10 seconds to clean up orderly.
         * If that fails, then the smartcard stack
         * is hung up and we need to die hard
         */
        alarm (10);
        signal (SIGALRM, on_alrm_signal);
}

static gboolean
after_term_signal (GIOChannel   *io_channel,
                   GIOCondition  condition,
                   gpointer      data)
{
        g_main_loop_quit (event_loop);
        return FALSE;
}

static void
on_debug_message (const char     *log_domain,
                  GLogLevelFlags  log_level,
                  const char     *message,
                  gpointer        user_data)
{
        g_printerr ("*** DEBUG: %s\n", message);
}

int
main (int    argc,
      char **argv)
{
        GIOChannel *io_channel;

        setlocale (LC_ALL, "");

        g_type_init ();

        g_log_set_handler (NULL, G_LOG_LEVEL_DEBUG, on_debug_message, NULL);

        event_loop = g_main_loop_new (NULL, FALSE);

        watch_for_smartcards ();

        if (pipe (signal_pipe_fds) != 0) {
                return 1;
        }
        fcntl (signal_pipe_fds[0], F_SETFD, FD_CLOEXEC);
        fcntl (signal_pipe_fds[1], F_SETFD, FD_CLOEXEC);

        io_channel = g_io_channel_unix_new (signal_pipe_fds[0]);
        g_io_channel_set_flags (io_channel, G_IO_FLAG_NONBLOCK, NULL);
        g_io_channel_set_encoding (io_channel, NULL, NULL);
        g_io_channel_set_buffered (io_channel, FALSE);
        g_io_add_watch (io_channel, G_IO_HUP, after_term_signal, NULL);
        g_io_channel_set_close_on_unref (io_channel, TRUE);
        g_io_channel_unref (io_channel);

        signal (SIGTERM, on_term_signal);
        signal (SIGPIPE, on_term_signal);

#ifdef HAVE_SYS_PRCTL_H
        prctl (PR_SET_PDEATHSIG, SIGKILL);
#endif

        g_main_loop_run (event_loop);

        stop_watching_for_smartcards ();

        return 0;
}
