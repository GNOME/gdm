#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sysexits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/vt.h>
#include <sys/kd.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>

#include <security/pam_appl.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <systemd/sd-daemon.h>
#include <systemd/sd-login.h>
#include <systemd/sd-journal.h>

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#endif /* HAVE_SELINUX */

#include "gdm-session-settings.h"

#ifndef GDM_PASSWD_AUXILLARY_BUFFER_SIZE
#define GDM_PASSWD_AUXILLARY_BUFFER_SIZE 1024
#endif

#ifndef GDM_SESSION_DEFAULT_PATH
#define GDM_SESSION_DEFAULT_PATH "/usr/local/bin:/usr/bin:/bin"
#endif

#ifndef GDM_SESSION_ROOT_UID
#define GDM_SESSION_ROOT_UID 0
#endif

#define RELEASE_DISPLAY_SIGNAL (SIGRTMAX)
#define ACQUIRE_DISPLAY_SIGNAL (SIGRTMAX - 1)

struct _State
{
        ActUserManager   *user_manager;
        ActUser          *user;
        GMainLoop        *event_loop;
        int               exit_code;

        pam_handle_t     *pam_handle;

        GPid              child_pid;
        guint             child_watch_id;

        char              *service;
        char              *username;

        char              *seat_id;
        char              *session_id;

        uid_t              uid;
        gid_t              gid;
        char              *shell;
        char              *home_directory;

        char              *session;
        char              *language;

        int                original_vt;
        int                session_vt;
        int                session_tty_fd;

        char             **arguments;
} state;

static int
pam_new_messages_handler (int                        number_of_messages,
                          const struct pam_message **messages,
                          struct pam_response      **responses,
                          State                     *state)
{
        g_debug ("gdm-run-in-new-session: %d new messages received from PAM\n", number_of_messages);

        if (number_of_messages == 0)
                return PAM_SUCCESS;

        return PAM_CONV_ERR;
}

static void
on_release_display (int signal)
{
        g_autofd int fd = -1;

        fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);
        ioctl (fd, VT_RELDISP, 1);
}

static void
on_acquire_display (int signal)
{
        g_autofd int fd = -1;

        fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);
        ioctl (fd, VT_RELDISP, VT_ACKACQ);
}

static gboolean
handle_terminal_vt_switches (State *state,
                             int    tty_fd)
{
        struct vt_mode setmode_request = { 0 };
        gboolean succeeded = TRUE;

        setmode_request.mode = VT_PROCESS;
        setmode_request.relsig = RELEASE_DISPLAY_SIGNAL;
        setmode_request.acqsig = ACQUIRE_DISPLAY_SIGNAL;

        if (ioctl (tty_fd, VT_SETMODE, &setmode_request) < 0) {
                g_debug ("gdm-run-in-new-session: couldn't manage VTs manually: %m");
                succeeded = FALSE;
        }

        signal (RELEASE_DISPLAY_SIGNAL, on_release_display);
        signal (ACQUIRE_DISPLAY_SIGNAL, on_acquire_display);

        return succeeded;
}

static void
fix_terminal_vt_mode (State  *state,
                      int     tty_fd)
{
        struct vt_mode getmode_reply = { 0 };
        int kernel_display_mode = 0;
        gboolean mode_fixed = FALSE;
        gboolean succeeded = TRUE;

        if (ioctl (tty_fd, VT_GETMODE, &getmode_reply) < 0) {
                g_debug ("gdm-run-in-new-session: Couldn't query VT mode: %m");
                succeeded = FALSE;
        }

        if (getmode_reply.mode != VT_AUTO) {
                goto out;
        }

        if (ioctl (tty_fd, KDGETMODE, &kernel_display_mode) < 0) {
                g_debug ("gdm-run-in-new-session: Couldn't query kernel display mode: %m");
                succeeded = FALSE;
        }

        if (kernel_display_mode == KD_TEXT) {
                goto out;
        }

        /* VT is in the anti-social state of VT_AUTO + KD_GRAPHICS,
         * fix it.
         */
        succeeded = handle_terminal_vt_switches (state, tty_fd);
        mode_fixed = TRUE;
out:
        if (!succeeded) {
                g_error ("gdm-run-in-new-session: Couldn't set up terminal, aborting...");
                return;
        }

        g_debug ("gdm-run-in-new-session: VT mode did %sneed to be fixed",
                 mode_fixed? "" : "not ");
}

static void
jump_to_vt (State  *state,
            int     vt_number)
{
        g_autofd int fd = -1;
        g_autofd int active_vt_tty_fd = -1;
        int active_vt = -1;
        struct vt_stat vt_state = { 0 };

        g_debug ("gdm-run-in-new-session: jumping to VT %d", vt_number);

        active_vt_tty_fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);

        if (state->session_tty_fd != -1) {
                static const char *clear_screen_escape_sequence = "\33[H\33[2J";

                /* let's make sure the new VT is clear */
                write (state->session_tty_fd,
                       clear_screen_escape_sequence,
                       sizeof (clear_screen_escape_sequence));

                fd = state->session_tty_fd;

                handle_terminal_vt_switches (state, fd);

                g_debug ("gdm-run-in-new-session: first setting graphics mode to prevent flicker");
                if (ioctl (fd, KDSETMODE, KD_GRAPHICS) < 0) {
                        g_debug ("gdm-run-in-new-session: Couldn't set graphics mode: %m");
                }
        } else {
                fd = g_steal_fd (&active_vt_tty_fd);
        }

        /* It's possible that the current VT was left in a broken
         * combination of states (KD_GRAPHICS with VT_AUTO), that
         * can't be switched away from.  This call makes sure things
         * are set in a way that VT_ACTIVATE should work and
         * VT_WAITACTIVE shouldn't hang.
         */
        fix_terminal_vt_mode (state, active_vt_tty_fd);

        if (ioctl (fd, VT_GETSTATE, &vt_state) < 0) {
                g_debug ("gdm-run-in-new-session: couldn't get current VT: %m");
        } else {
                active_vt = vt_state.v_active;
        }

        if (active_vt != vt_number) {
                if (ioctl (fd, VT_ACTIVATE, vt_number) < 0) {
                        g_debug ("gdm-run-in-new-session: couldn't initiate jump to VT %d: %m",
                                 vt_number);
                } else if (ioctl (fd, VT_WAITACTIVE, vt_number) < 0) {
                        g_debug ("gdm-run-in-new-session: couldn't finalize jump to VT %d: %m",
                                 vt_number);
                }
        }
}

static void
quit (int exit_code)
{
        if (state->pam_handle != NULL)
                uninitialize_pam (state, exit_code);

        if (state->child_pid > 0)
                killpg (state->child_pid, SIGHUP);

        state->exit_code = exit_code;
        g_main_loop_quit (state->event_loop);
}

static void
on_forked_to_start_session (State *state)
{
        g_autofree char *home_dir = NULL;
        const char * const * environment = NULL;

        int stdin_fd = -1, stdout_fd = -1, stderr_fd = -1;
        gboolean needs_controlling_terminal = FALSE;

        if (state->session_tty_fd > 0) {
                dup2 (state->session_tty_fd, STDIN_FILENO);
                close (state->session_tty_fd);
                state->session_tty_fd = -1;
                needs_controlling_terminal = TRUE;
        } else {
                stdin_fd = open ("/dev/null", O_RDWR);
                dup2 (stdin_fd, STDIN_FILENO);
                close (stdin_fd);
        }

        if (setsid () < 0) {
                g_debug ("gdm-run-in-new-session: Could not set pid '%u' as leader of new session and process group: %s",
                         (guint) getpid (), g_strerror (errno));
                _exit (EXIT_FAILURE);
        }

        if (needs_controlling_terminal) {
                if (ioctl (STDIN_FILENO, TIOCSCTTY, 0) < 0) {
                        g_debug ("gdm-run-in-new-session: Could not take control of tty: %m");
                }
        }

        if (setuid (state->uid) < 0) {
                g_debug ("gdm-run-in-new-session: Could not reset uid: %m");
                _exit (EXIT_FAILURE);
        }

        environment = (const char * const *) pam_getenvlist (worker->pam_handle);

        home_dir = get_environment_variable (state, "HOME");

        if ((home_dir == NULL) || g_chdir (home_dir) < 0) {
                g_chdir ("/");
        }

        stdout_fd = sd_journal_stream_fd (state->arguments[0], LOG_INFO, FALSE);
        stderr_fd = sd_journal_stream_fd (state->arguments[0], LOG_WARNING, FALSE);

        /* Unset the CLOEXEC flags, because sd_journal_stream_fd
         * gives it to us by default.
         */
        gdm_clear_close_on_exec_flag (stdout_fd);
        gdm_clear_close_on_exec_flag (stderr_fd);

        if (stdout_fd != -1) {
                dup2 (stdout_fd, STDOUT_FILENO);
                close (stdout_fd);
        }

        if (stderr_fd != -1) {
                dup2 (stderr_fd, STDERR_FILENO);
                close (stderr_fd);
        }

        signal (SIGPIPE, SIG_DFL);

        execvpe (state->arguments[0],
                 state->arguments,
                 (char **)
                 environment);
}

static void
uninitialize_pam (State *state)
{
        int status;

        if (state->exit_code == EX_OK)
                 status = PAM_SUCCESS;
        else
                 status = PAM_ABORT;

        pam_close_session (state->pam_handle, PAM_SILENT);
        pam_end (state->pam_handle, status);
}

static void
on_child_finished (GPid   pid,
                   int    status,
                   State *state)
{
        int exit_code;

        g_debug ("gdm-run-in-new-session: child (pid:%d) done (%s:%d)",
                 (int) pid,
                 WIFEXITED (status) ? "status"
                 : WIFSIGNALED (status) ? "signal"
                 : "unknown",
                 WIFEXITED (status) ? WEXITSTATUS (status)
                 : WIFSIGNALED (status) ? WTERMSIG (status)
                 : -1);

        if (state->original_vt > 0)
                jump_to_vt (state, state->original_vt);

        if (WIFEXITED (status)) {
                exit_code = WEXITSTATUS (status);
        } else if (WIFSIGNALED (status)) {
                if (WTERMSIG (status) == SIGTERM)
                        exit_code = 0;
                else
                        exit_code = 128 + WTERMSIG (status);
        }

        quit (exit_code);
}

static void
start_session (State *state)
{
        struct passwd *passwd_entry;
        pid_t session_pid;

        gdm_get_pwent_for_name (state->username, &passwd_entry);

        g_debug ("gdm-run-in-new-session: starting user session with program '%s'",
                 state->arguments[0]);

        jump_to_vt (state, state->session_vt);

        session_pid = fork ();

        if (session_pid < 0) {
                quit (EX_OSERR);
                return;
        }

        if (session_pid == 0) {
            on_forked_to_start_session (state);
            g_debug ("gdm-run-in-new-session: Command '%s' could not be started: %s",
                     state->arguments[0],
                     g_strerror (errno));

            _exit (EXIT_FAILURE);
        }

        g_clear_fd (&state->session_tty_fd, NULL);

#ifdef HAVE_SELINUX
        setexeccon (NULL);
#endif

        state->child_pid = session_pid;
        state->child_watch_id = g_child_watch_add (state->child_pid,
                                                   (GChildWatchFunc)
                                                   on_child_finished,
                                                   state);
}

static void
open_session (State *state)
{
        int error_code;

        error_code = pam_open_session (state->pam_handle, PAM_SILENT);

        if (error_code != PAM_SUCCESS) {
                g_debug ("gdm-run-in-new-session: Unable to look_up account info");
                quit (EX_OSERR);
                return;
        }

        state_session (&state);
}

static void
on_user_is_loaded_changed (State *state)
{
        if (!act_user_is_loaded (state->user)) {
                g_debug ("gdm-run-in-new-session: User manager said user %s may have loaded, but it didn't", state->username);
                return;
        }

        g_debug ("gdm-run-in-new-session: User %s loaded", state->username);

        state->session = act_user_get_session (state->user);
        state->language = act_user_get_language (state->user);

        open_session (state);
}

static void
accredit_user (State *state)
{
        g_autofree char *shell = NULL;
        g_autofree char *home = NULL;
        gboolean ret;
        int      error_code;

        ret = FALSE;
 
        if (state->username == NULL) {
                g_debug ("gdm-run-in-new-session: Username not set");
                quit (EX_OSERR);
                return;
        }

        ret = look_up_passwd_info (state->username,
                                   &state->uid,
                                   &state->gid,
                                   &state->home,
                                   &state->shell);
        if (! rer) {
                g_debug ("gdm-run-in-new-session: Unable to look_up account info");
                quit (EX_OSERR);
                return;
        }

        set_environment_variable (state, "XDG_SEAT", state->seat_id);
        set_environment_variable (state, "LOGNAME", state->username);
        set_environment_variable (state, "USER", state->username);
        set_environment_variable (state, "USERNAME", state->username);
        set_environment_variable (state, "HOME", state->home_directory);
        set_environment_variable (state, "PWD", state->home_directory);
        set_environment_variable (state, "SHELL", state->shell);

        error_code = pam_setcred (state->pam_handle, PAM_SILENT);

        if (error_code != PAM_SUCCESS) {
                g_warning ("User %s cannot silently establish credentials",
                           state->username, pam_strerror (state->pam_handle, error_code));
                quit (EX_OSERR);
                return;
        }

        state.user = act_user_manager_get_user (state->user_manager, state->username);
        g_signal_connect_swapped (state->user,
                                  "notify::is-loaded",
                                  G_CALLBACK (on_user_is_loaded_changed),
                                  &state);
}

static void
authorize_user (State *state)
{
        int error_code;
        char *username;

        g_debug ("gdm-run-in-new-session: determining if authenticated user is authorized to session");

        /* check that the account isn't disabled or expired
         */
        error_code = pam_acct_mgmt (state->pam_handle, PAM_SILENT);

        if (error_code != PAM_SUCCESS) {
                g_warning ("User %s is not authorized to silently log in: %s",
                           state->username, pam_strerror (state->pam_handle, error_code));
                quit (EX_OSERR);
                return;
        }

        error_code = pam_get_item (state->pam_handle, PAM_USER, &username);

        if (error_code == PAM_SUCCESS) {
                g_debug ("gdm-run-in-new-session: After authorization, canonical username is '%s'",
                         username);
                g_set_str (&state->username, username);
        }

        accredit_user (state);
}

static void
authenticate_user (State *state)
{
        int error_code;
        int authentication_flags = PAM_SILENT;

        g_debug ("gdm-run-in-new-session: authenticating user %s", state->username);

        error_code = pam_authenticate (state->pam_handle, authentication_flags);

        if (error_code != PAM_SUCCESS) {
                g_warning ("User %s is cannot be silently authenticated: %s",
                           state->username, pam_strerror (state->pam_handle, error_code));
                quit (EX_OSERR);
                return;
        }

        authorize_user (state);
}

static void
initialize_pam (State *state)
{
        struct pam_conv pam_conversation;
        int             error_code;

        g_assert (state->pam_handle == NULL);

        g_debug ("gdm-run-in-new-session: initializing PAM; service=%s username=%s seat=%s",
                 state->service,
                 state->username,
                 state->seat_id ?: "(no seat)");

        pam_conversation.conv = (StatePamNewMessagesFunc) pam_new_messages_handler;
        pam_conversation.appdata_ptr = state;

        start_auditor (state);
        error_code = pam_start (state->service,
                                state->username,
                                &pam_conversation,
                                &state->pam_handle);

        if (error_code != PAM_SUCCESS) {
                g_debug ("gdm-run-in-new-session: could not initialize PAM: (error code %d)", error_code);
                quit (EX_OSERR);
                return;
        }

        if (seat_id != NULL') {
                set_environment_variable (state, "XDG_SEAT", seat_id);
        }

        set_environment_variable (state, "XDG_SESSION_CLASS", "user");

        if (g_strcmp0 (service->seat_id, "seat0") == 0) {
                pam_set_item (state->pam_handle, PAM_TTY, seat->tty_path);
                set_environment_variable (state, "XDG_VTNR", seat->session_vt);
        }

        authenticate_user (state);
}

static void
set_environment_variable (State      *state,
                          const char *key,
                          const char *value)
{
        g_autofree char *environment_entry = NULL;
        int error_code;

        if (value != NULL) {
                environment_entry = g_strdup_printf ("%s=%s", key, value);
        } else {
                /* empty value means "remove from environment" */
                environment_entry = g_strdup (key);
        }

        error_code = pam_putenv (state->pam_handle,
                                 environment_entry);

        if (error_code != PAM_SUCCESS) {
                g_warning ("cannot put %s in pam environment: %s\n",
                           environment_entry,
                           pam_strerror (state->pam_handle, error_code));
        }
        g_debug ("gdm-run-in-new-session: Set PAM environment variable: '%s'", environment_entry);
}

static char *
get_environment_variable (State      *state,
                          const char *key)
{
        return g_strdup (pam_getenv (state->pam_handle, key));
}

static gboolean
environment_variable_is_set (State     *state,
                            const char *key)
{
        return pam_getenv (state->pam_handle, key) != NULL;
}

static gboolean
look_up_passwd_info (const char *username,
                     uid_t      *uid,
                     gid_t      *gid,
                     char      **home_directory,
                     char      **shell)
{
        gboolean       ret;
        struct passwd *passwd_entry;
        struct passwd  passwd_buffer;
        g_autofree char *aux_buffer = NULL;
        long           required_aux_buffer_size;
        gsize          aux_buffer_size;

        ret = FALSE;
        aux_buffer = NULL;

        required_aux_buffer_size = sysconf (_SC_GETPW_R_SIZE_MAX);

        if (required_aux_buffer_size < 0) {
                aux_buffer_size = GDM_PASSWD_AUXILLARY_BUFFER_SIZE;
        } else {
                aux_buffer_size = (gsize) required_aux_buffer_size;
        }

        aux_buffer = g_new0 (aux_buffer_size);

        /* we use the _r variant of getpwnam()
         * (with its weird semantics) so that the
         * passwd_entry doesn't potentially get stomped on
         * by a PAM module
         */
 again:
        passwd_entry = NULL;
        errno = getpwnam_r (username,
                            &passwd_buffer,
                            aux_buffer,
                            (size_t) aux_buffer_size,
                            &passwd_entry);
        if (errno == EINTR) {
                goto again;
        } else if (errno != 0) {
                g_warning ("gdm-run-in-new-session: Could not look up user info: %s", g_strerror (errno));
                return;
        }

        if (passwd_entry == NULL) {
                goto out;
        }

        if (uid != NULL) {
                *uid = passwd_entry->pw_uid;
        }
        if (gid != NULL) {
                *gid = passwd_entry->pw_gid;
        }
        if (home_directory != NULL) {
                if (passwd_entry->pw_dir != NULL && passwd_entry->pw_dir[0] != '\0') {
                        g_set_str (home_directory, passwd_entry->pw_dir);
                } else {
                        g_set_str (home_directory, "/");
                }
        }
        if (shellp != NULL) {
                if (passwd_entry->pw_shell != NULL && passwd_entry->pw_shell[0] != '\0') {
                        g_set_str (shell, passwd_entry->pw_shell);
                } else {
                        g_set_str (shell, "/bin/bash");
                }
        }

        return TRUE;
}

static void
get_current_vt (int *out_vt)
{
        g_autofd int fd = -1;
        struct vt_stat vt_state;

        fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);

        if (fd < 0) {
                g_debug ("gdm-run-in-new-session: Couldn't open VT controller: %m");
                return;
        }

        if (ioctl (fd, VT_GETSTATE, &vt_state) < 0) {
                g_debug ("gdm-run-in-new-session: Couldn't querry current VT state: %m");
                return;
        }

        *out_vt = vt_state.v_active;
}

static int
find_free_vt (int *out_vt)
{
        g_autofd int tty_fd = -1;
        g_autofree char *tty_string = NULL;
        g_autofree char *vt_string = NULL;
        g_autofd int initial_vt_fd = -1;
        int session_vt = 0;

        /* We open the initial VT so it's marked "in use" and
         * doesn't get returned by VT_OPENQRY
         */
        tty_string = g_strdup_printf ("/dev/tty%d", GDM_INITIAL_VT);
        initial_vt_fd = open (tty_string, O_RDWR | O_NOCTTY);
        g_clear_pointer (&tty_string);

        if (initial_vt_fd < 0) {
                g_debug ("gdm-run-in-new-session: Couldn't open console of VT %d: %m", GDM_INITIAL_VT);
                return FALSE;
        }

        if (ioctl (initial_vt_fd, VT_OPENQRY, &session_vt) < 0) {
                g_debug ("gdm-run-in-new-session: couldn't open new VT: %m");
                return FALSE;
        }

        *out_tty_fd = -1;

        if (session_vt <= 0)
                return -1;

        vt_string = g_strdup_printf ("%d", session_vt);

        tty_string = g_strdup_printf ("/dev/tty%d", session_vt);
        tty_fd = open (tty_string, O_RDWR);

        if (tty_fd < 0)
                return -1;

        *vt = session_vt;
        return g_steal_fd (&tty_fd);
}

static void
on_user_manager_is_loaded_changed (State *state)
{
        gboolean is_loaded;

        g_object_get (G_OBJECT (state->user_manager),
                      "is-loaded", &is_loaded,
                      NULL);

        if (!is_loaded) {
                g_debug ("gdm-run-in-new-session: User manager said it may have loaded, but it didn't");
                return;
        }

        initialize_pam (state);
}

int
main(int argc, char *argv[])
{
        g_autoptr (GError) error = NULL;
        g_autoptr (GOptionContext) context = NULL;
        char *username = "root";
        char *service = "gdm-autologin";
        char *session_type = "tty";
        char *session_desktop = NULL;
        int vt = 0;
        g_autofree char *tty_path = NULL;
        g_autofd int tty_fd = -1;
        gboolean no_vt = FALSE;
        g_auto (GStrv) remaining_arguments = NULL;

        GOptionEntry entries[] =
        {
                { "user", 'u', 0, G_OPTION_ARG_STRING, &username, "Username for which to run the program", "USER" },
                { "service", 's', 0, G_OPTION_ARG_STRING, &service, "PAM service to use", "SERVICE" },
                { "session-type", 't', 0, G_OPTION_ARG_STRING, &session_type, "e.g., x11, wayland, or tty", "TYPE" },
                { "vt", 'v', 0, G_OPTION_ARG_INT, &vt, "VT to run on", "VT" },
                { "no-vt", 'n', 0, G_OPTION_ARG_STRING, &no_vt, "Don't run on a VT", NULL },
                { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &remaining_arguments, NULL, "[COMMAND...]" },
                { NULL }
        };

        context = g_option_context_new ("- Run a program in a logind session with specific environment variables as a specified user");
        g_option_context_add_main_entries (context, entries, NULL);

        if (!g_option_context_parse (context, &argc, &argv, &error)) {
                g_printerr ("Option parsing failed: %s\n", error->message);
                return EX_USAGE;
        }

        if (vt > 0 && no_vt) {
                g_printerr ("--vt and --no-vt can't be specified at the same time\n");
                return EX_USAGE;
        }

        if (vt < 0 || vt > MAX_NR_CONSOLES) {
                g_printerr ("--vt requires an argument in the range of %d to %d", 1, MAX_NR_CONSOLES);
                return EX_USAGE;
        }

        if (!no_vt) {
                get_current_vt (&state->original_vt);
                g_debug ("gdm-run-in-new-session: Original VT is %d", state->orignal_vt);
        }

        if (vt == 0 && !no_vt) {
                tty_fd = find_free_vt (&vt);
        } else if (vt != 0) {
                state->session_vt = vt;
                tty_path = g_strdup_printf ("/dev/tty%d", vt);

                g_debug ("gdm-run-in-new-session: Using TTY path %s", tty_path);
                tty_fd = open (tty_path, O_RDWR | O_APPEND);
        }

        if (tty_fd < 0) {
                tty_path = g_strdup_printf ("/dev/null");
                tty_fd = open (tty_path, O_RDWR | O_APPEND);
        }

        if (vt != 0) {
                g_debug ("gdm-run-in-new-session: Using seat %s", "seat0");
                state.seat_id = g_strdup ("seat0");
        }

        g_debug ("gdm-run-in-new-session: Using username %s", username);
        state.username = g_strdup (username);

        g_debug ("gdm-run-in-new-session: Using PAM service %s", service);
        state.service = g_strdup (service);

        g_debug ("gdm-run-in-new-session: Using session type %s", session_type);
        state.session_type = g_strdup (session_type);

        if (remaining_arguments != NULL) {
                g_autofree char *command = g_strjoinv (" ", remaining_arguments);

                g_debug ("gdm-run-in-new-session: Using command %s", command);
                state.arguments = g_strdupv (remaining_arguments);
        }

        state.user_manager = act_user_manager_get_default ();
        state.user = act_user_manager_get_user (state.user_manager, username);

        g_signal_connect_swapped (state.user,
                                  "notify::is-loaded",
                                  G_CALLBACK (on_user_manager_is_loaded_changed),
                                  &state);

        state.event_loop = g_main_loop_new (NULL, FALSE);
        state.exit_code = EX_SOFTWARE;

        g_main_loop_run (state.event_loop);

        return state->exit_code;
}

