#include "config.h"

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
#include <utmpx.h>
#include <sys/mman.h>

#include "session-child.h"
#include "session.h"
#include "console-kit.h"
#include "privileges.h"
#include "xauthority.h"

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
    int length;

    length = value ? strlen (value) : -1;
    write_data (&length, sizeof (length));
    if (value)
        write_data (value, sizeof (char) * length);
}

static ssize_t
read_data (void *buf, size_t count)
{
    ssize_t n_read;

    n_read = read (from_daemon_output, buf, count);
    if (n_read < 0)
        g_printerr ("Error reading from daemon: %s\n", strerror (errno));
  
    return n_read;
}

static gchar *
read_string_full (void* (*alloc_fn)(size_t n))
{
    int length;
    char *value;

    if (read_data (&length, sizeof (length)) <= 0)
        return NULL;
    if (length < 0)
        return NULL;
    if (length > MAX_STRING_LENGTH)
    {
        g_printerr ("Invalid string length %d from daemon\n", length);
        return NULL;
    }
  
    value = (*alloc_fn) (sizeof (char) * (length + 1));
    read_data (value, length);
    value[length] = '\0';      

    return value;
}

static gchar *
read_string ()
{
    return read_string_full (g_malloc);
}

static int
pam_conv_cb (int msg_length, const struct pam_message **msg, struct pam_response **resp, void *app_data)
{
    int i, error;
    gboolean auth_complete = FALSE;
    struct pam_response *response;
    gchar *username = NULL;

    /* FIXME: We don't support communication after pam_authenticate completes */
    if (authentication_complete)
        return PAM_SUCCESS;

    /* Cancel authentication if requiring input */
    if (!is_interactive)
    {
        for (i = 0; i < msg_length; i++)
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
    pam_get_item (pam_handle, PAM_USER, (const void **) &username);

    /* Notify the daemon */
    write_string (username);
    write_data (&auth_complete, sizeof (auth_complete));
    write_data (&msg_length, sizeof (msg_length));
    for (i = 0; i < msg_length; i++)
    {
        const struct pam_message *m = msg[i];
        write_data (&m->msg_style, sizeof (m->msg_style));
        write_string (m->msg);
    }

    /* Get response */
    read_data (&error, sizeof (error));
    if (error != PAM_SUCCESS)
        return error;
    response = calloc (msg_length, sizeof (struct pam_response));
    for (i = 0; i < msg_length; i++)
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

int
session_child_run (int argc, char **argv)
{
    struct pam_conv conversation = { pam_conv_cb, NULL };
    int i, version, fd, result;
    gboolean auth_complete = TRUE;
    User *user = NULL;
    gchar *log_filename, *log_backup_filename = NULL;
    gsize env_length;
    gsize command_argc;
    gchar **command_argv;
#ifdef WITH_CONSOLEKIT
    GVariantBuilder ck_parameters;
#endif
    int return_code;
    int authentication_result;
    gchar *authentication_result_string;
    gchar *service;
    gchar *username;
    gchar *class;
    gchar *tty;
    gchar *remote_host_name;
    gchar *xdisplay;
    gchar *xauth_name;
    XAuthority *xauthority = NULL;
    gchar *xauth_filename;
    GDBusConnection *bus;
#ifdef WITH_CONSOLEKIT
    gchar *console_kit_cookie = NULL;
#endif
#ifdef WITH_LOGIND
    gchar *systemd_logind_session = NULL;
#endif

    const gchar *path;
    GError *error = NULL;

#if !defined(GLIB_VERSION_2_36)
    g_type_init ();
#endif

    if (config_get_boolean (config_get_instance (), "LightDM", "lock-memory"))
    {
        /* Protect memory from being paged to disk, as we deal with passwords */
        mlockall (MCL_CURRENT | MCL_FUTURE);
    }

    /* Make input non-blocking */
    fd = open ("/dev/null", O_RDONLY);
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
    read_data (&version, sizeof (version));

    service = read_string ();
    username = read_string ();
    read_data (&do_authenticate, sizeof (do_authenticate));
    read_data (&is_interactive, sizeof (is_interactive));
    class = read_string ();
    tty = read_string ();
    remote_host_name = read_string ();
    xdisplay = read_string ();
    xauth_name = read_string ();
    if (xauth_name)
    {
        guint16 xauth_family;
        guint8 *xauth_address;
        gsize xauth_address_length;
        gchar *xauth_number;
        guint8 *xauth_data;
        gsize xauth_data_length;

        read_data (&xauth_family, sizeof (xauth_family));
        read_data (&xauth_address_length, sizeof (xauth_address_length));
        xauth_address = g_malloc (xauth_address_length);
        read_data (xauth_address, xauth_address_length);
        xauth_number = read_string ();
        read_data (&xauth_data_length, sizeof (xauth_data_length));
        xauth_data = g_malloc (xauth_data_length);
        read_data (xauth_data, xauth_data_length);

        xauthority = xauth_new (xauth_family, xauth_address, xauth_address_length, xauth_number, xauth_name, xauth_data, xauth_data_length);
    }

    /* Setup PAM */
    result = pam_start (service, username, &conversation, &pam_handle);
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
    if (xauthority)
    {
        struct pam_xauth_data value;

        value.name = (char *) xauth_get_authorization_name (xauthority);
        value.namelen = strlen (xauth_get_authorization_name (xauthority));
        value.data = (char *) xauth_get_authorization_data (xauthority);
        value.datalen = xauth_get_authorization_data_length (xauthority);
        pam_set_item (pam_handle, PAM_XAUTHDATA, &value);
    }
#endif

    /* Authenticate */
    if (do_authenticate)
    {
        const gchar *new_username;

        authentication_result = pam_authenticate (pam_handle, 0);

        /* See what user we ended up as */
        if (pam_get_item (pam_handle, PAM_USER, (const void **) &new_username) != PAM_SUCCESS)
            return EXIT_FAILURE;
        g_free (username);
        username = g_strdup (new_username);

        /* Check account is valid */
        if (authentication_result == PAM_SUCCESS)
            authentication_result = pam_acct_mgmt (pam_handle, 0);
        if (authentication_result == PAM_NEW_AUTHTOK_REQD)
            authentication_result = pam_chauthtok (pam_handle, 0);
    }
    else
        authentication_result = PAM_SUCCESS;
    authentication_complete = TRUE;

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
        }
    }

    authentication_result_string = g_strdup (pam_strerror (pam_handle, authentication_result));

    /* Report authentication result */
    write_string (username);
    write_data (&auth_complete, sizeof (auth_complete));
    write_data (&authentication_result, sizeof (authentication_result));
    write_string (authentication_result_string);

    /* Check we got a valid user */
    if (!username)
    {
        g_printerr ("No user selected during authentication\n");
        return EXIT_FAILURE;
    }

    /* Stop if we didn't authenticated */
    if (authentication_result != PAM_SUCCESS)
        return EXIT_FAILURE;

    /* Get the command to run (blocks) */
    log_filename = read_string ();
    xauth_filename = read_string ();
    read_data (&env_length, sizeof (env_length));
    for (i = 0; i < env_length; i++)
        pam_putenv (pam_handle, read_string ());
    read_data (&command_argc, sizeof (command_argc));
    command_argv = g_malloc (sizeof (gchar *) * (command_argc + 1));
    for (i = 0; i < command_argc; i++)
        command_argv[i] = read_string ();
    command_argv[i] = NULL;

    /* Redirect stderr to a log file */
    if (log_filename)
        log_backup_filename = g_strdup_printf ("%s.old", log_filename);
    if (!log_filename)
    {
        fd = open ("/dev/null", O_WRONLY);   
        dup2 (fd, STDERR_FILENO);
        close (fd);
    }
    else if (g_path_is_absolute (log_filename))
    {
        rename (log_filename, log_backup_filename);
        fd = open (log_filename, O_WRONLY | O_APPEND | O_CREAT, 0600);
        dup2 (fd, STDERR_FILENO);
        close (fd);
    }

    /* Set group membership - these can be overriden in pam_setcred */
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
        return EXIT_FAILURE;
    }
     
    /* Open the session */
    result = pam_open_session (pam_handle, 0);
    if (result != PAM_SUCCESS)
    {
        g_printerr ("Failed to open PAM session: %s\n", pam_strerror (pam_handle, result));
        return EXIT_FAILURE;
    }

    /* Open a connection to the system bus for ConsoleKit - we must keep it open or CK will close the session */
    bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error)
        g_printerr ("Unable to contact system bus: %s", error->message);
    if (!bus)
        return EXIT_FAILURE;

#ifdef WITH_LOGIND
    *systemd_logind_session = logind_get_session_id ();
    write_string (systemd_logind_session);
#endif
#ifdef WITH_CONSOLEKIT
    /* Open a Console Kit session */
    if (!LOGIND_RUNNING())
    {
        g_variant_builder_init (&ck_parameters, G_VARIANT_TYPE ("(a(sv))"));
        g_variant_builder_open (&ck_parameters, G_VARIANT_TYPE ("a(sv)"));
        g_variant_builder_add (&ck_parameters, "(sv)", "unix-user", g_variant_new_int32 (user_get_uid (user)));
        if (g_strcmp0 (class, XDG_SESSION_CLASS_GREETER) == 0)
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
	write_string (console_kit_cookie);
        if (console_kit_cookie)
        {
            gchar *value;
            value = g_strdup_printf ("XDG_SESSION_COOKIE=%s", console_kit_cookie);
            pam_putenv (pam_handle, value);
            g_free (value);
        }
    }
#endif

    /* Write X authority */
    if (xauthority)
    {
        gboolean drop_privileges, result;
        gchar *value;
        GError *error = NULL;

        drop_privileges = geteuid () == 0;
        if (drop_privileges)
            privileges_drop (user);
        result = xauth_write (xauthority, XAUTH_WRITE_MODE_REPLACE, xauth_filename, &error);
        if (drop_privileges)
            privileges_reclaim ();

        if (error)
            g_printerr ("Error writing X authority: %s\n", error->message);
        g_clear_error (&error);
        if (!result)
            return EXIT_FAILURE;

        value = g_strdup_printf ("XAUTHORITY=%s", xauth_filename);
        pam_putenv (pam_handle, value);
        g_free (value);
    }

    /* Put our tools directory in the path as a hack so we can use the legacy gdmflexiserver interface */
    path = pam_getenv (pam_handle, "PATH");
    if (path)
        pam_putenv (pam_handle, g_strdup_printf ("PATH=%s:%s", PKGLIBEXEC_DIR, path));

    /* Catch terminate signal and pass it to the child */
    signal (SIGTERM, signal_cb);

    /* Run the command as the authenticated user */
    child_pid = fork (); 
    if (child_pid == 0)
    {
        // FIXME: This is not thread safe (particularly the printfs)

        /* Make this process its own session */
        if (setsid () < 0)
            g_printerr ("Failed to make process a new session: %s\n", strerror (errno));

        /* Change to this user */
        if (getuid () == 0)
        {
            if (setgid (user_get_gid (user)) != 0)
            {
                g_printerr ("Failed to set group ID to %d: %s\n", user_get_gid (user), strerror (errno));
                _exit (EXIT_FAILURE);
            }

            if (setuid (user_get_uid (user)) != 0)
            {
                g_printerr ("Failed to set user ID to %d: %s\n", user_get_uid (user), strerror (errno));
                _exit (EXIT_FAILURE);
            }
        }

        /* Change working directory */
        /* NOTE: This must be done after the permissions are changed because NFS filesystems can
         * be setup so the local root user accesses the NFS files as 'nobody'.  If the home directories
         * are not system readable then the chdir can fail */
        if (chdir (user_get_home_directory (user)) != 0)
        {
            g_printerr ("Failed to change to home directory %s: %s\n", user_get_home_directory (user), strerror (errno));
            _exit (EXIT_FAILURE);
        }

        /* Redirect stderr to a log file */
        if (log_filename && !g_path_is_absolute (log_filename))
        {
            rename (log_filename, log_backup_filename);
            fd = open (log_filename, O_WRONLY | O_APPEND | O_CREAT, 0600);
            dup2 (fd, STDERR_FILENO);
            close (fd);
        }

        /* Run the command */
        execve (command_argv[0], command_argv, pam_getenvlist (pam_handle));
        g_printerr ("Failed to run command: %s\n", strerror (errno));
        _exit (EXIT_FAILURE);
    }

    /* Bail out if failed to fork */
    if (child_pid < 0)
    {
        g_printerr ("Failed to fork session child process: %s\n", strerror (errno));
        return_code = EXIT_FAILURE;
    }

    /* Wait for the command to complete (blocks) */
    if (child_pid > 0)
    {
        /* Log to utmp */
        if (g_strcmp0 (class, XDG_SESSION_CLASS_GREETER) != 0)
        {
            struct utmpx ut;
            struct timeval tv;

            memset (&ut, 0, sizeof (ut));
            ut.ut_type = USER_PROCESS;
            ut.ut_pid = child_pid;
            strncpy (ut.ut_line, tty + strlen ("/dev/"), sizeof (ut.ut_line));
            strncpy (ut.ut_id, xdisplay, sizeof (ut.ut_id));
            strncpy (ut.ut_user, username, sizeof (ut.ut_user));
            if (xdisplay)
                strncpy (ut.ut_host, xdisplay, sizeof (ut.ut_host));
            else if (remote_host_name)
                strncpy (ut.ut_host, remote_host_name, sizeof (ut.ut_host));
            gettimeofday (&tv, NULL);
            ut.ut_tv.tv_sec = tv.tv_sec;
            ut.ut_tv.tv_usec = tv.tv_usec;

            setutxent ();
            if (!pututxline (&ut))
                g_printerr ("Failed to write utmpx: %s\n", strerror (errno));
            endutxent ();
        }

        waitpid (child_pid, &return_code, 0);
        child_pid = 0;

        /* Log to utmp */
        if (g_strcmp0 (class, XDG_SESSION_CLASS_GREETER) != 0)
        {
            struct utmpx ut;
            struct timeval tv;

            memset (&ut, 0, sizeof (ut));
            ut.ut_type = DEAD_PROCESS;
            ut.ut_pid = child_pid;
            strncpy (ut.ut_line, tty + strlen ("/dev/"), sizeof (ut.ut_line));
            strncpy (ut.ut_id, xdisplay, sizeof (ut.ut_id));
            strncpy (ut.ut_user, username, sizeof (ut.ut_user));
            if (xdisplay)
                strncpy (ut.ut_host, xdisplay, sizeof (ut.ut_host));
            else if (remote_host_name)
                strncpy (ut.ut_host, remote_host_name, sizeof (ut.ut_host));
            gettimeofday (&tv, NULL);
            ut.ut_tv.tv_sec = tv.tv_sec;
            ut.ut_tv.tv_usec = tv.tv_usec;

            setutxent ();
            if (!pututxline (&ut))
                g_printerr ("Failed to write utmpx: %s\n", strerror (errno));
            endutxent ();
        }
    }

    /* Remove X authority */
    if (xauthority)
    {
        gboolean drop_privileges, result;
        GError *error = NULL;

        drop_privileges = geteuid () == 0;
        if (drop_privileges)
            privileges_drop (user);
        result = xauth_write (xauthority, XAUTH_WRITE_MODE_REMOVE, xauth_filename, &error);
        if (drop_privileges)
            privileges_reclaim ();

        if (error)
            g_printerr ("Error removing X authority: %s\n", error->message);
        g_clear_error (&error);
        if (!result)
            _exit (EXIT_FAILURE);
    }

#ifdef WITH_CONSOLEKIT
    /* Close the Console Kit session */
    if (console_kit_cookie)
        ck_close_session (console_kit_cookie);
#endif

    /* Close the session */
    pam_close_session (pam_handle, 0);

    /* Remove credentials */
    result = pam_setcred (pam_handle, PAM_DELETE_CRED);

    pam_end (pam_handle, 0);
    pam_handle = NULL;

    /* Return result of session process to the daemon */
    return return_code;
}
