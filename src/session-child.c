#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <glib.h>
#include <security/pam_appl.h>
#include <utmp.h>
#include <utmpx.h>
#include <sys/mman.h>

#if HAVE_LIBAUDIT
#include <libaudit.h>
#endif

#include "configuration.h"
#include "session-child.h"
#include "session.h"
#include "console-kit.h"
#include "login1.h"
#include "log-file.h"
#include "privileges.h"
#include "x-authority.h"
#include "configuration.h"

/* Child process being run */
static GPid child_pid = 0;

/* Pipe to communicate with daemon */
static int from_daemon_output = 0;
static int to_daemon_input = 0;

static gboolean is_interactive;
static gboolean do_authenticate;
static gboolean authentication_complete = FALSE;
static pam_handle_t *pam_handle;

/* Maximum length of a string to pass between daemon and session */
#define MAX_STRING_LENGTH 65535

static void
write_data (const void *buf, size_t count)
{
    if (write (to_daemon_input, buf, count) != count)
        g_printerr ("Error writing to daemon: %s\n", strerror (errno));
}

static void
write_string (const char *value)
{
    int length = value ? strlen (value) : -1;
    write_data (&length, sizeof (length));
    if (value)
        write_data (value, sizeof (char) * length);
}

static ssize_t
read_data (void *buf, size_t count)
{
    ssize_t n_read = read (from_daemon_output, buf, count);
    if (n_read < 0)
        g_printerr ("Error reading from daemon: %s\n", strerror (errno));

    return n_read;
}

static gchar *
read_string_full (void* (*alloc_fn)(size_t n))
{
    int length;
    if (read_data (&length, sizeof (length)) <= 0)
        return NULL;
    if (length < 0)
        return NULL;
    if (length > MAX_STRING_LENGTH)
    {
        g_printerr ("Invalid string length %d from daemon\n", length);
        return NULL;
    }

    gchar *value = (*alloc_fn) (sizeof (gchar) * (length + 1));
    read_data (value, length);
    value[length] = '\0';

    return value;
}

static gchar *
read_string (void)
{
    return read_string_full (g_malloc);
}

static int
pam_conv_cb (int msg_length, const struct pam_message **msg, struct pam_response **resp, void *app_data)
{
    /* FIXME: We don't support communication after pam_authenticate completes */
    if (authentication_complete)
        return PAM_SUCCESS;

    /* Cancel authentication if requiring input */
    if (!is_interactive)
    {
        for (int i = 0; i < msg_length; i++)
        {
            if (msg[i]->msg_style == PAM_PROMPT_ECHO_ON || msg[i]->msg_style == PAM_PROMPT_ECHO_OFF)
            {
                g_printerr ("Stopping PAM conversation, interaction requested but not supported\n");
                return PAM_CONV_ERR;
            }
        }

        /* Ignore informational messages */
        return PAM_SUCCESS;
    }

    /* Check if we changed user */
    gchar *username = NULL;
    pam_get_item (pam_handle, PAM_USER, (const void **) &username);

    /* Notify the daemon */
    write_string (username);
    gboolean auth_complete = FALSE;
    write_data (&auth_complete, sizeof (auth_complete));
    size_t length = msg_length;
    write_data (&length, sizeof (length));
    for (int i = 0; i < msg_length; i++)
    {
        const struct pam_message *m = msg[i];
        write_data (&m->msg_style, sizeof (m->msg_style));
        write_string (m->msg);
    }

    /* Get response */
    int error;
    read_data (&error, sizeof (error));
    if (error != PAM_SUCCESS)
        return error;
    struct pam_response *response = calloc (msg_length, sizeof (struct pam_response));
    for (int i = 0; i < msg_length; i++)
    {
        struct pam_response *r = &response[i];
        // callers of this function inside pam will expect to be able to call
        // free() on the strings we give back.  So alloc with malloc.
        r->resp = read_string_full (malloc);
        read_data (&r->resp_retcode, sizeof (r->resp_retcode));
    }

    *resp = response;
    return PAM_SUCCESS;
}

static void
signal_cb (int signum)
{
    /* Pass on signal to child, otherwise just quit */
    if (child_pid > 0)
        kill (child_pid, signum);
    else
        exit (EXIT_SUCCESS);
}

static XAuthority *
read_xauth (void)
{
    g_autofree gchar *x_authority_name = read_string ();
    if (!x_authority_name)
        return NULL;

    guint16 x_authority_family;
    read_data (&x_authority_family, sizeof (x_authority_family));

    gsize x_authority_address_length;
    read_data (&x_authority_address_length, sizeof (x_authority_address_length));
    g_autofree guint8 *x_authority_address = g_malloc (x_authority_address_length);
    read_data (x_authority_address, x_authority_address_length);

    g_autofree gchar *x_authority_number = read_string ();

    gsize x_authority_data_length;
    read_data (&x_authority_data_length, sizeof (x_authority_data_length));
    g_autofree guint8 *x_authority_data = g_malloc (x_authority_data_length);
    read_data (x_authority_data, x_authority_data_length);

    return x_authority_new (x_authority_family, x_authority_address, x_authority_address_length, x_authority_number, x_authority_name, x_authority_data, x_authority_data_length);
}

/* GNU provides this but we can't rely on that so let's make our own version */
#if defined(__GLIBC__)
static void
updwtmpx (const gchar *wtmp_file, struct utmpx *ut)
#else // use this version in libc's other than glibc
void
updwtmpx (const char *wtmp_file, const struct utmpx *ut)
#endif
{
    struct utmp u;
    memset (&u, 0, sizeof (u));
    u.ut_type = ut->ut_type;
    u.ut_pid = ut->ut_pid;
    if (ut->ut_line)
        strncpy (u.ut_line, ut->ut_line, sizeof (u.ut_line));
    if (ut->ut_id)
        strncpy (u.ut_id, ut->ut_id, sizeof (u.ut_id));
    if (ut->ut_user)
        strncpy (u.ut_user, ut->ut_user, sizeof (u.ut_user));
    if (ut->ut_host)
        strncpy (u.ut_host, ut->ut_host, sizeof (u.ut_host));
    u.ut_tv.tv_sec = ut->ut_tv.tv_sec;
    u.ut_tv.tv_usec = ut->ut_tv.tv_usec;

    updwtmp (wtmp_file, &u);
}

#if HAVE_LIBAUDIT
static void
audit_event (int type, const gchar *username, uid_t uid, const gchar *remote_host_name, const gchar *tty, gboolean success)
{
    int auditfd = audit_open ();
    if (auditfd < 0) {
        g_printerr ("Error opening audit socket: %s\n", strerror (errno));
        return;
    }

    const char *op = NULL;
    if (type == AUDIT_USER_LOGIN)
        op = "login";
    else if (type == AUDIT_USER_LOGOUT)
        op = "logout";
    int result = success == TRUE ? 1 : 0;

    if (audit_log_acct_message (auditfd, type, NULL, op, username, uid, remote_host_name, NULL, tty, result) <= 0)
        g_printerr ("Error writing audit message: %s\n", strerror (errno));

    close (auditfd);
}
#endif

int
session_child_run (int argc, char **argv)
{
#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    if (config_get_boolean (config_get_instance (), "LightDM", "lock-memory"))
    {
        /* Protect memory from being paged to disk, as we deal with passwords */
        mlockall (MCL_CURRENT | MCL_FUTURE);
    }

    /* Make input non-blocking */
    int fd = open ("/dev/null", O_RDONLY);
    dup2 (fd, STDIN_FILENO);
    close (fd);

    /* Close stdout */
    fd = open ("/dev/null", O_WRONLY);
    dup2 (fd, STDOUT_FILENO);
    close (fd);

    /* Get the pipe from the daemon */
    if (argc != 4)
    {
        g_printerr ("Usage: lightdm --session-child INPUTFD OUTPUTFD\n");
        return EXIT_FAILURE;
    }
    from_daemon_output = atoi (argv[2]);
    to_daemon_input = atoi (argv[3]);
    if (from_daemon_output == 0 || to_daemon_input == 0)
    {
        g_printerr ("Invalid file descriptors %s %s\n", argv[2], argv[3]);
        return EXIT_FAILURE;
    }

    /* Don't let these pipes leak to the command we will run */
    fcntl (from_daemon_output, F_SETFD, FD_CLOEXEC);
    fcntl (to_daemon_input, F_SETFD, FD_CLOEXEC);

    /* Read a version number so we can handle upgrades (i.e. a newer version of session child is run for an old daemon */
    int version;
    read_data (&version, sizeof (version));

    g_autofree gchar *service = read_string ();
    g_autofree gchar *username = read_string ();
    read_data (&do_authenticate, sizeof (do_authenticate));
    read_data (&is_interactive, sizeof (is_interactive));
    g_autofree gchar *unused_class = read_string (); /* Used to be class, now we just use the environment variable */
    g_autofree gchar *tty = read_string ();
    g_autofree gchar *remote_host_name = read_string ();
    g_autofree gchar *xdisplay = read_string ();
    g_autoptr(XAuthority) x_authority = read_xauth ();

    /* Setup PAM */
    struct pam_conv conversation = { pam_conv_cb, NULL };
    int result = pam_start (service, username, &conversation, &pam_handle);
    if (result != PAM_SUCCESS)
    {
        g_printerr ("Failed to start PAM: %s", pam_strerror (NULL, result));
        return EXIT_FAILURE;
    }
    if (xdisplay)
    {
#ifdef PAM_XDISPLAY
        pam_set_item (pam_handle, PAM_XDISPLAY, xdisplay);
#endif
        pam_set_item (pam_handle, PAM_TTY, xdisplay);
    }
    else if (tty)
        pam_set_item (pam_handle, PAM_TTY, tty);

#ifdef PAM_XAUTHDATA
    if (x_authority)
    {
        struct pam_xauth_data value;

        value.name = (char *) x_authority_get_authorization_name (x_authority);
        value.namelen = strlen (x_authority_get_authorization_name (x_authority));
        value.data = (char *) x_authority_get_authorization_data (x_authority);
        value.datalen = x_authority_get_authorization_data_length (x_authority);
        pam_set_item (pam_handle, PAM_XAUTHDATA, &value);
    }
#endif

    /* Authenticate */
    int authentication_result = PAM_SUCCESS;
    if (do_authenticate)
    {
        const gchar *new_username;

        authentication_result = pam_authenticate (pam_handle, 0);

        /* See what user we ended up as */
        if (pam_get_item (pam_handle, PAM_USER, (const void **) &new_username) != PAM_SUCCESS)
        {
            pam_end (pam_handle, 0);
            return EXIT_FAILURE;
        }
        g_free (username);
        username = g_strdup (new_username);

        /* Write record to btmp database */
        if (authentication_result == PAM_AUTH_ERR)
        {
            struct utmpx ut;
            struct timeval tv;

            memset (&ut, 0, sizeof (ut));
            ut.ut_type = USER_PROCESS;
            ut.ut_pid = getpid ();
            if (xdisplay)
                strncpy (ut.ut_id, xdisplay, sizeof (ut.ut_id));
            if (tty && g_str_has_prefix (tty, "/dev/"))
                strncpy (ut.ut_line, tty + strlen ("/dev/"), sizeof (ut.ut_line));
            strncpy (ut.ut_user, username, sizeof (ut.ut_user));
            if (xdisplay)
                strncpy (ut.ut_host, xdisplay, sizeof (ut.ut_host));
            else if (remote_host_name)
                strncpy (ut.ut_host, remote_host_name, sizeof (ut.ut_host));
            gettimeofday (&tv, NULL);
            ut.ut_tv.tv_sec = tv.tv_sec;
            ut.ut_tv.tv_usec = tv.tv_usec;

            updwtmpx ("/var/log/btmp", &ut);

#if HAVE_LIBAUDIT
            audit_event (AUDIT_USER_LOGIN, username, -1, remote_host_name, tty, FALSE);
#endif
        }

        /* Check account is valid */
        if (authentication_result == PAM_SUCCESS)
            authentication_result = pam_acct_mgmt (pam_handle, 0);
        if (authentication_result == PAM_NEW_AUTHTOK_REQD)
            authentication_result = pam_chauthtok (pam_handle, PAM_CHANGE_EXPIRED_AUTHTOK);
    }
    else
        authentication_result = PAM_SUCCESS;
    authentication_complete = TRUE;

    User *user = NULL;
    if (authentication_result == PAM_SUCCESS)
    {
        /* Fail authentication if user doesn't actually exist */
        user = accounts_get_user_by_name (username);
        if (!user)
        {
            g_printerr ("Failed to get information on user %s: %s\n", username, strerror (errno));
            authentication_result = PAM_USER_UNKNOWN;
        }
        else
        {
            /* Set POSIX variables */
            pam_putenv (pam_handle, "PATH=/usr/local/bin:/usr/bin:/bin");
            pam_putenv (pam_handle, g_strdup_printf ("USER=%s", username));
            pam_putenv (pam_handle, g_strdup_printf ("LOGNAME=%s", username));
            pam_putenv (pam_handle, g_strdup_printf ("HOME=%s", user_get_home_directory (user)));
            pam_putenv (pam_handle, g_strdup_printf ("SHELL=%s", user_get_shell (user)));

            /* Let the greeter and user session inherit the system default locale */
            static const gchar * const locale_var_names[] = {
                "LC_PAPER",
                "LC_NAME",
                "LC_ADDRESS",
                "LC_TELEPHONE",
                "LC_MEASUREMENT",
                "LC_IDENTIFICATION",
                "LC_COLLATE",
                "LC_CTYPE",
                "LC_MONETARY",
                "LC_NUMERIC",
                "LC_TIME",
                "LC_MESSAGES",
                "LC_ALL",
                "LANG",
                NULL
            };
            for (int i = 0; locale_var_names[i] != NULL; i++)
            {
                const gchar *locale_value;
                if ((locale_value = g_getenv (locale_var_names[i])) != NULL)
                {
                    g_autofree gchar *locale_var = g_strdup_printf ("%s=%s", locale_var_names[i], locale_value);
                    pam_putenv (pam_handle, locale_var);
                }
            }
        }
    }

    g_autofree gchar *authentication_result_string = g_strdup (pam_strerror (pam_handle, authentication_result));

    /* Report authentication result */
    write_string (username);
    gboolean auth_complete = TRUE;
    write_data (&auth_complete, sizeof (auth_complete));
    write_data (&authentication_result, sizeof (authentication_result));
    write_string (authentication_result_string);

    /* Check we got a valid user */
    if (!username)
    {
        g_printerr ("No user selected during authentication\n");
        pam_end (pam_handle, 0);
        return EXIT_FAILURE;
    }

    /* Stop if we didn't authenticated */
    if (authentication_result != PAM_SUCCESS)
    {
        pam_end (pam_handle, 0);
        return EXIT_FAILURE;
    }

    /* Get the command to run (blocks) */
    g_autofree gchar *log_filename = read_string ();
    LogMode log_mode = LOG_MODE_BACKUP_AND_TRUNCATE;
    if (version >= 3)
        read_data (&log_mode, sizeof (log_mode));
    if (version >= 1)
    {
        g_free (tty);
        tty = read_string ();
    }
    g_autofree gchar *x_authority_filename = read_string ();
    if (version >= 1)
    {
        g_free (xdisplay);
        xdisplay = read_string ();
        g_clear_object (&x_authority);
        x_authority = read_xauth ();
    }
    gsize env_length;
    read_data (&env_length, sizeof (env_length));
    for (int i = 0; i < env_length; i++)
        pam_putenv (pam_handle, read_string ());
    gsize command_argc;
    read_data (&command_argc, sizeof (command_argc));
    g_auto(GStrv) command_argv = g_malloc (sizeof (gchar *) * (command_argc + 1));
    int i;
    for (i = 0; i < command_argc; i++)
        command_argv[i] = read_string ();
    command_argv[i] = NULL;

    /* If nothing to run just refresh credentials because we successfully authenticated */
    if (command_argc == 0)
    {
        pam_setcred (pam_handle, PAM_REINITIALIZE_CRED);
        pam_end (pam_handle, 0);
        return EXIT_SUCCESS;
    }

    /* Redirect stderr to a log file */
    if (log_filename)
    {
        if (g_path_is_absolute (log_filename))
        {
            int fd = log_file_open (log_filename, log_mode);
            dup2 (fd, STDERR_FILENO);
            close (fd);
            g_clear_pointer (&log_filename, g_free);
        }
    }
    else
    {
        int fd = open ("/dev/null", O_WRONLY);
        dup2 (fd, STDERR_FILENO);
        close (fd);
    }

    /* Set group membership - these can be overridden in pam_setcred */
    if (getuid () == 0)
    {
        if (initgroups (username, user_get_gid (user)) < 0)
        {
            g_printerr ("Failed to initialize supplementary groups for %s: %s\n", username, strerror (errno));
            _exit (EXIT_FAILURE);
        }
    }

    /* Set credentials */
    result = pam_setcred (pam_handle, PAM_ESTABLISH_CRED);
    if (result != PAM_SUCCESS)
    {
        g_printerr ("Failed to establish PAM credentials: %s\n", pam_strerror (pam_handle, result));
        pam_end (pam_handle, 0);
        return EXIT_FAILURE;
    }

    /* Open the session */
    result = pam_open_session (pam_handle, 0);
    if (result != PAM_SUCCESS)
    {
        g_printerr ("Failed to open PAM session: %s\n", pam_strerror (pam_handle, result));
        pam_end (pam_handle, 0);
        return EXIT_FAILURE;
    }

    /* Open a connection to the system bus for ConsoleKit - we must keep it open or CK will close the session */
    g_autoptr(GError) error = NULL;
    g_autoptr(GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_printerr ("Unable to contact system bus: %s", error->message);
    if (!bus)
    {
        pam_end (pam_handle, 0);
        return EXIT_FAILURE;
    }

    /* Check what logind session we are, or fallback to ConsoleKit */
    const gchar *login1_session_id = pam_getenv (pam_handle, "XDG_SESSION_ID");
    g_autofree gchar *console_kit_cookie = NULL;
    if (login1_session_id)
    {
        write_string (login1_session_id);
        if (version >= 2)
            write_string (NULL);
    }
    else
    {
        GVariantBuilder ck_parameters;
        g_variant_builder_init (&ck_parameters, G_VARIANT_TYPE ("(a(sv))"));
        g_variant_builder_open (&ck_parameters, G_VARIANT_TYPE ("a(sv)"));
        g_variant_builder_add (&ck_parameters, "(sv)", "unix-user", g_variant_new_int32 (user_get_uid (user)));
        if (g_strcmp0 (pam_getenv (pam_handle, "XDG_SESSION_CLASS"), "greeter") == 0)
            g_variant_builder_add (&ck_parameters, "(sv)", "session-type", g_variant_new_string ("LoginWindow"));
        if (xdisplay)
        {
            g_variant_builder_add (&ck_parameters, "(sv)", "x11-display", g_variant_new_string (xdisplay));
            if (tty)
                g_variant_builder_add (&ck_parameters, "(sv)", "x11-display-device", g_variant_new_string (tty));
        }
        if (remote_host_name)
        {
            g_variant_builder_add (&ck_parameters, "(sv)", "is-local", g_variant_new_boolean (FALSE));
            g_variant_builder_add (&ck_parameters, "(sv)", "remote-host-name", g_variant_new_string (remote_host_name));
        }
        else
            g_variant_builder_add (&ck_parameters, "(sv)", "is-local", g_variant_new_boolean (TRUE));
        console_kit_cookie = ck_open_session (&ck_parameters);
        if (version >= 2)
            write_string (NULL);
        write_string (console_kit_cookie);
        if (console_kit_cookie)
        {
            g_autofree gchar *value = NULL;
            g_autofree gchar *runtime_dir = NULL;
            value = g_strdup_printf ("XDG_SESSION_COOKIE=%s", console_kit_cookie);
            pam_putenv (pam_handle, value);

            runtime_dir = ck_get_xdg_runtime_dir (console_kit_cookie);
            if (runtime_dir)
            {
                g_autofree gchar *v = g_strdup_printf ("XDG_RUNTIME_DIR=%s", runtime_dir);
                pam_putenv (pam_handle, v);
            }
        }
    }

    /* Write X authority */
    if (x_authority)
    {
        gboolean drop_privileges = geteuid () == 0;
        if (drop_privileges)
            privileges_drop (user_get_uid (user), user_get_gid (user));

        g_autoptr(GError) error = NULL;
        gboolean result = x_authority_write (x_authority, XAUTH_WRITE_MODE_REPLACE, x_authority_filename, &error);
        if (drop_privileges)
            privileges_reclaim ();

        if (error)
            g_printerr ("Error writing X authority: %s\n", error->message);
        if (!result)
        {
            pam_end (pam_handle, 0);
            return EXIT_FAILURE;
        }

        g_autofree gchar *value = g_strdup_printf ("XAUTHORITY=%s", x_authority_filename);
        pam_putenv (pam_handle, value);
    }

    /* Catch terminate signal and pass it to the child */
    signal (SIGTERM, signal_cb);

    /* Run the command as the authenticated user */
    uid_t uid = user_get_uid (user);
    gid_t gid = user_get_gid (user);
    const gchar *home_directory = user_get_home_directory (user);
    child_pid = fork ();
    if (child_pid == 0)
    {
        /* Make this process its own session */
        if (setsid () < 0)
            _exit (errno);

        /* Change to this user */
        if (getuid () == 0)
        {
            if (setgid (gid) != 0)
                _exit (errno);

            if (setuid (uid) != 0)
                _exit (errno);
        }

        /* Change working directory */
        /* NOTE: This must be done after the permissions are changed because NFS filesystems can
         * be setup so the local root user accesses the NFS files as 'nobody'.  If the home directories
         * are not system readable then the chdir can fail */
        if (chdir (home_directory) != 0)
            _exit (errno);

        if (log_filename)
        {
            int fd = log_file_open (log_filename, log_mode);
            if (fd >= 0)
            {
                dup2 (fd, STDERR_FILENO);
                close (fd);
            }
        }

        /* Reset SIGPIPE handler so the child has default behaviour (we disabled it at LightDM start) */
        signal (SIGPIPE, SIG_DFL);

        /* Run the command */
        execve (command_argv[0], command_argv, pam_getenvlist (pam_handle));
        _exit (EXIT_FAILURE);
    }

    /* Bail out if failed to fork */
    int return_code = EXIT_SUCCESS;
    if (child_pid < 0)
    {
        g_printerr ("Failed to fork session child process: %s\n", strerror (errno));
        return_code = EXIT_FAILURE;
    }

    /* Wait for the command to complete (blocks) */
    if (child_pid > 0)
    {
        /* Log to utmp */
        if (g_strcmp0 (pam_getenv (pam_handle, "XDG_SESSION_CLASS"), "greeter") != 0)
        {
            struct utmpx ut;
            memset (&ut, 0, sizeof (ut));
            ut.ut_type = USER_PROCESS;
            ut.ut_pid = child_pid;
            if (xdisplay)
                strncpy (ut.ut_id, xdisplay, sizeof (ut.ut_id));
            if (tty && g_str_has_prefix (tty, "/dev/"))
                strncpy (ut.ut_line, tty + strlen ("/dev/"), sizeof (ut.ut_line));
            strncpy (ut.ut_user, username, sizeof (ut.ut_user));
            if (xdisplay)
                strncpy (ut.ut_host, xdisplay, sizeof (ut.ut_host));
            else if (remote_host_name)
                strncpy (ut.ut_host, remote_host_name, sizeof (ut.ut_host));
            struct timeval tv;
            gettimeofday (&tv, NULL);
            ut.ut_tv.tv_sec = tv.tv_sec;
            ut.ut_tv.tv_usec = tv.tv_usec;

            /* Write records to utmp/wtmp databases */
            setutxent ();
            if (!pututxline (&ut))
                g_printerr ("Failed to write utmpx: %s\n", strerror (errno));
            endutxent ();
            updwtmpx ("/var/log/wtmp", &ut);

#if HAVE_LIBAUDIT
            audit_event (AUDIT_USER_LOGIN, username, uid, remote_host_name, tty, TRUE);
#endif
        }

        int child_status;
        waitpid (child_pid, &child_status, 0);
        child_pid = 0;
        if (WIFEXITED (child_status))
            return_code = WEXITSTATUS (child_status);
        else
            return_code = EXIT_FAILURE;

        /* Log to utmp */
        if (g_strcmp0 (pam_getenv (pam_handle, "XDG_SESSION_CLASS"), "greeter") != 0)
        {
            struct utmpx ut;
            memset (&ut, 0, sizeof (ut));
            ut.ut_type = DEAD_PROCESS;
            ut.ut_pid = child_pid;
            if (xdisplay)
                strncpy (ut.ut_id, xdisplay, sizeof (ut.ut_id));
            if (tty && g_str_has_prefix (tty, "/dev/"))
                strncpy (ut.ut_line, tty + strlen ("/dev/"), sizeof (ut.ut_line));
            strncpy (ut.ut_user, username, sizeof (ut.ut_user));
            if (xdisplay)
                strncpy (ut.ut_host, xdisplay, sizeof (ut.ut_host));
            else if (remote_host_name)
                strncpy (ut.ut_host, remote_host_name, sizeof (ut.ut_host));
            struct timeval tv;
            gettimeofday (&tv, NULL);
            ut.ut_tv.tv_sec = tv.tv_sec;
            ut.ut_tv.tv_usec = tv.tv_usec;

            /* Write records to utmp/wtmp databases */
            setutxent ();
            if (!pututxline (&ut))
                g_printerr ("Failed to write utmpx: %s\n", strerror (errno));
            endutxent ();
            updwtmpx ("/var/log/wtmp", &ut);

#if HAVE_LIBAUDIT
            audit_event (AUDIT_USER_LOGOUT, username, uid, remote_host_name, tty, TRUE);
#endif
        }
    }

    /* Remove X authority */
    if (x_authority)
    {
        gboolean drop_privileges = geteuid () == 0;
        if (drop_privileges)
            privileges_drop (user_get_uid (user), user_get_gid (user));

        g_autoptr(GError) error = NULL;
        x_authority_write (x_authority, XAUTH_WRITE_MODE_REMOVE, x_authority_filename, &error);
        if (drop_privileges)
            privileges_reclaim ();

        if (error)
            g_printerr ("Error removing X authority: %s\n", error->message);
        g_clear_error (&error);
        /* Ignore this error, don't exit, continue closing the session. */
    }

    /* Close the Console Kit session */
    if (console_kit_cookie)
        ck_close_session (console_kit_cookie);

    /* Close the session */
    pam_close_session (pam_handle, 0);

    /* Remove credentials */
    pam_setcred (pam_handle, PAM_DELETE_CRED);

    pam_end (pam_handle, 0);
    pam_handle = NULL;

    /* Return result of session process to the daemon */
    return return_code;
}
