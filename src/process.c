/*
 * Copyright (C) 2010-2011 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <grp.h>
#include <config.h>

#include "log-file.h"
#include "process.h"

enum {
    GOT_DATA,
    GOT_SIGNAL,
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct
{
    /* Function to run inside subprocess before exec */
    ProcessRunFunc run_func;
    gpointer run_func_data;

    /* File to log to */
    gchar *log_file;
    gboolean log_stdout;
    LogMode log_mode;

    /* Command to run */
    gchar *command;

    /* TRUE to clear the environment in this process */
    gboolean clear_environment;

    /* Environment variables to set */
    GHashTable *env;

    /* Process ID */
    GPid pid;

    /* Exit status of process */
    int exit_status;

    /* TRUE if stopping this process (waiting for child process to stop) */
    gboolean stopping;

    /* Timeout waiting for process to quit */
    guint quit_timeout;

    /* Watch on process */
    guint watch;
} ProcessPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (Process, process, G_TYPE_OBJECT)

static Process *current_process = NULL;
static GHashTable *processes = NULL;
static pid_t signal_pid;
static int signal_pipe[2];

#ifndef HAVE_CLEARENV
extern char **environ;
#endif

Process *
process_get_current (void)
{
    if (current_process)
        return current_process;

    current_process = process_new (NULL, NULL);
    ProcessPrivate *priv = process_get_instance_private (current_process);
    priv->pid = getpid ();

    return current_process;
}

Process *
process_new (ProcessRunFunc run_func, gpointer run_func_data)
{
    Process *process = g_object_new (PROCESS_TYPE, NULL);
    ProcessPrivate *priv = process_get_instance_private (process);

    priv->run_func = run_func;
    priv->run_func_data = run_func_data;
    priv->log_mode = LOG_MODE_INVALID;
    return process;
}

void
process_set_log_file (Process *process, const gchar *path, gboolean log_stdout, LogMode log_mode)
{
    ProcessPrivate *priv = process_get_instance_private (process);

    g_return_if_fail (process != NULL);

    g_free (priv->log_file);
    priv->log_file = g_strdup (path);
    priv->log_stdout = log_stdout;
    priv->log_mode = log_mode;
}

void
process_set_clear_environment (Process *process, gboolean clear_environment)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    g_return_if_fail (process != NULL);
    priv->clear_environment = clear_environment;
}

gboolean
process_get_clear_environment (Process *process)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    g_return_val_if_fail (process != NULL, FALSE);
    return priv->clear_environment;
}

void
process_set_env (Process *process, const gchar *name, const gchar *value)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    g_return_if_fail (process != NULL);
    g_return_if_fail (name != NULL);
    g_hash_table_insert (priv->env, g_strdup (name), g_strdup (value));
}

const gchar *
process_get_env (Process *process, const gchar *name)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    g_return_val_if_fail (process != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    return g_hash_table_lookup (priv->env, name);
}

void
process_set_command (Process *process, const gchar *command)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    g_return_if_fail (process != NULL);
    g_free (priv->command);
    priv->command = g_strdup (command);
}

const gchar *
process_get_command (Process *process)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    g_return_val_if_fail (process != NULL, NULL);
    return priv->command;
}

static void
process_watch_cb (GPid pid, gint status, gpointer data)
{
    Process *process = data;
    ProcessPrivate *priv = process_get_instance_private (process);

    priv->watch = 0;
    priv->exit_status = status;

    if (WIFEXITED (status))
        g_debug ("Process %d exited with return value %d", pid, WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        g_debug ("Process %d terminated with signal %d", pid, WTERMSIG (status));

    if (priv->quit_timeout)
        g_source_remove (priv->quit_timeout);
    priv->quit_timeout = 0;
    priv->pid = 0;
    g_hash_table_remove (processes, GINT_TO_POINTER (pid));

    g_signal_emit (process, signals[STOPPED], 0);
}

gboolean
process_start (Process *process, gboolean block)
{
    ProcessPrivate *priv = process_get_instance_private (process);

    g_return_val_if_fail (process != NULL, FALSE);
    g_return_val_if_fail (priv->command != NULL, FALSE);
    g_return_val_if_fail (priv->pid == 0, FALSE);

    gint argc;
    g_auto(GStrv) argv = NULL;
    g_autoptr(GError) error = NULL;
    if (!g_shell_parse_argv (priv->command, &argc, &argv, &error))
    {
        g_warning ("Error parsing command %s: %s", priv->command, error->message);
        return FALSE;
    }

    int log_fd = -1;
    if (priv->log_file)
        log_fd = log_file_open (priv->log_file, priv->log_mode);

    /* Work out variables to set */
    guint env_length = g_hash_table_size (priv->env);
    g_autofree gchar **env_keys = g_malloc (sizeof (gchar *) * env_length);
    g_autofree gchar **env_values = g_malloc (sizeof (gchar *) * env_length);
    GList *keys = g_hash_table_get_keys (priv->env);
    guint i = 0;
    for (GList *link = keys; i < env_length; i++, link = link->next)
    {
        env_keys[i] = link->data;
        env_values[i] = g_hash_table_lookup (priv->env, env_keys[i]);
    }
    g_list_free (keys);

    pid_t pid = fork ();
    if (pid == 0)
    {
        /* Do custom setup */
        if (priv->run_func)
            priv->run_func (process, priv->run_func_data);

        /* Redirect output to logfile */
        if (log_fd >= 0)
        {
             if (priv->log_stdout)
                 dup2 (log_fd, STDOUT_FILENO);
             dup2 (log_fd, STDERR_FILENO);
             close (log_fd);
        }

        /* Set environment */
        if (priv->clear_environment)
#ifdef HAVE_CLEARENV
            clearenv ();
#else
            environ = NULL;
#endif
        for (guint i = 0; i < env_length; i++)
        {
            const gchar *value = env_values[i];
            if (value != NULL)
                setenv (env_keys[i], value, TRUE);
            else
                unsetenv (env_keys[i]);
        }

        /* Reset SIGPIPE handler so the child has default behaviour (we disabled it at LightDM start) */
        signal (SIGPIPE, SIG_DFL);

        execvp (argv[0], argv);
        _exit (EXIT_FAILURE);
    }

    close (log_fd);

    if (pid < 0)
    {
        g_warning ("Failed to fork: %s", strerror (errno));
        return FALSE;
    }

    g_debug ("Launching process %d: %s", pid, priv->command);

    priv->pid = pid;

    if (block)
    {
        int exit_status;
        waitpid (priv->pid, &exit_status, 0);
        process_watch_cb (priv->pid, exit_status, process);
    }
    else
    {
        g_hash_table_insert (processes, GINT_TO_POINTER (priv->pid), g_object_ref (process));
        priv->watch = g_child_watch_add (priv->pid, process_watch_cb, process);
    }

    return TRUE;
}

gboolean
process_get_is_running (Process *process)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    g_return_val_if_fail (process != NULL, FALSE);
    return priv->pid != 0;
}

GPid
process_get_pid (Process *process)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    g_return_val_if_fail (process != NULL, 0);
    return priv->pid;
}

void
process_signal (Process *process, int signum)
{
    ProcessPrivate *priv = process_get_instance_private (process);

    g_return_if_fail (process != NULL);

    if (priv->pid == 0)
        return;

    g_debug ("Sending signal %d to process %d", signum, priv->pid);

    if (kill (priv->pid, signum) < 0)
    {
        /* Ignore ESRCH, we will pick that up in our wait */
        if (errno != ESRCH)
            g_warning ("Error sending signal %d to process %d: %s", signum, priv->pid, strerror (errno));
    }
}

static gboolean
quit_timeout_cb (Process *process)
{
    ProcessPrivate *priv = process_get_instance_private (process);

    priv->quit_timeout = 0;
    process_signal (process, SIGKILL);

    return FALSE;
}

void
process_stop (Process *process)
{
    ProcessPrivate *priv = process_get_instance_private (process);

    g_return_if_fail (process != NULL);

    if (priv->stopping)
        return;
    priv->stopping = TRUE;

    /* If already stopped then we're done! */
    if (priv->pid == 0)
        return;

    /* Send SIGTERM, and then SIGKILL if no response */
    priv->quit_timeout = g_timeout_add (5000, (GSourceFunc) quit_timeout_cb, process);
    process_signal (process, SIGTERM);
}

int
process_get_exit_status (Process *process)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    g_return_val_if_fail (process != NULL, -1);
    return priv->exit_status;
}

static void
process_init (Process *process)
{
    ProcessPrivate *priv = process_get_instance_private (process);
    priv->env = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
process_stopped (Process *process)
{
}

static void
process_finalize (GObject *object)
{
    Process *self = PROCESS (object);
    ProcessPrivate *priv = process_get_instance_private (self);

    if (priv->pid > 0)
        g_hash_table_remove (processes, GINT_TO_POINTER (priv->pid));

    g_clear_pointer (&priv->log_file, g_free);
    g_clear_pointer (&priv->command, g_free);
    g_hash_table_unref (priv->env);
    if (priv->quit_timeout)
        g_source_remove (priv->quit_timeout);
    if (priv->watch)
        g_source_remove (priv->watch);

    if (priv->pid)
        kill (priv->pid, SIGTERM);

    G_OBJECT_CLASS (process_parent_class)->finalize (object);
}

static void
signal_cb (int signum, siginfo_t *info, void *data)
{
    /* Check if we are from a forked process that hasn't updated the signal handlers or execed.
       If so, then we should just quit */
    if (getpid () != signal_pid)
        _exit (EXIT_SUCCESS);

    /* Write signal to main thread, if something goes wrong just close the pipe so it is detected on the other end */
    if (write (signal_pipe[1], &info->si_signo, sizeof (int)) < 0 ||
        write (signal_pipe[1], &info->si_pid, sizeof (pid_t)) < 0)
        close (signal_pipe[1]);
}

static gboolean
handle_signal (GIOChannel *source, GIOCondition condition, gpointer data)
{
    errno = 0;
    int signo;
    pid_t pid;
    if (read (signal_pipe[0], &signo, sizeof (int)) != sizeof (int) ||
        read (signal_pipe[0], &pid, sizeof (pid_t)) != sizeof (pid_t))
    {
        g_warning ("Error reading from signal pipe: %s", strerror (errno));
        return FALSE;
    }

    g_debug ("Got signal %d from process %d", signo, pid);

    Process *process = g_hash_table_lookup (processes, GINT_TO_POINTER (pid));
    if (process == NULL)
        process = process_get_current ();
    if (process)
        g_signal_emit (process, signals[GOT_SIGNAL], 0, signo);

    return TRUE;
}

static void
process_class_init (ProcessClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    struct sigaction action;

    klass->stopped = process_stopped;
    object_class->finalize = process_finalize;

    signals[GOT_DATA] =
        g_signal_new (PROCESS_SIGNAL_GOT_DATA,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (ProcessClass, got_data),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
    signals[GOT_SIGNAL] =
        g_signal_new (PROCESS_SIGNAL_GOT_SIGNAL,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (ProcessClass, got_signal),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);
    signals[STOPPED] =
        g_signal_new (PROCESS_SIGNAL_STOPPED,
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (ProcessClass, stopped),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /* Catch signals and feed them to the main loop via a pipe */
    processes = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
    signal_pid = getpid ();
    if (pipe (signal_pipe) != 0)
        g_critical ("Failed to create signal pipe");
    fcntl (signal_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl (signal_pipe[1], F_SETFD, FD_CLOEXEC);
    g_io_add_watch (g_io_channel_unix_new (signal_pipe[0]), G_IO_IN, handle_signal, NULL);
    action.sa_sigaction = signal_cb;
    sigemptyset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGUSR1, &action, NULL);
    sigaction (SIGUSR2, &action, NULL);
}
