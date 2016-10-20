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

struct ProcessPrivate
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
};

G_DEFINE_TYPE (Process, process, G_TYPE_OBJECT);

static Process *current_process = NULL;
static GHashTable *processes = NULL;
static pid_t signal_pid;
static int signal_pipe[2];

Process *
process_get_current (void)
{
    if (current_process)
        return current_process;

    current_process = process_new (NULL, NULL);
    current_process->priv->pid = getpid ();

    return current_process;
}

Process *
process_new (ProcessRunFunc run_func, gpointer run_func_data)
{
    Process *process = g_object_new (PROCESS_TYPE, NULL);
    process->priv->run_func = run_func;
    process->priv->run_func_data = run_func_data;
    process->priv->log_mode = LOG_MODE_INVALID;
    return process;
}

void
process_set_log_file (Process *process, const gchar *path, gboolean log_stdout, LogMode log_mode)
{
    g_return_if_fail (process != NULL);
    g_free (process->priv->log_file);
    process->priv->log_file = g_strdup (path);
    process->priv->log_stdout = log_stdout;
    process->priv->log_mode = log_mode;
}

void
process_set_clear_environment (Process *process, gboolean clear_environment)
{
    g_return_if_fail (process != NULL);
    process->priv->clear_environment = clear_environment;
}

gboolean
process_get_clear_environment (Process *process)
{
    g_return_val_if_fail (process != NULL, FALSE);
    return process->priv->clear_environment;
}

void
process_set_env (Process *process, const gchar *name, const gchar *value)
{
    g_return_if_fail (process != NULL);
    g_return_if_fail (name != NULL);
    g_hash_table_insert (process->priv->env, g_strdup (name), g_strdup (value));  
}

const gchar *
process_get_env (Process *process, const gchar *name)
{
    g_return_val_if_fail (process != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);
    return g_hash_table_lookup (process->priv->env, name);
}

void
process_set_command (Process *process, const gchar *command)
{
    g_return_if_fail (process != NULL);

    g_free (process->priv->command);
    process->priv->command = g_strdup (command);
}

const gchar *
process_get_command (Process *process)
{
    g_return_val_if_fail (process != NULL, NULL);
    return process->priv->command;
}

static void
process_watch_cb (GPid pid, gint status, gpointer data)
{
    Process *process = data;

    process->priv->watch = 0;
    process->priv->exit_status = status;

    if (WIFEXITED (status))
        g_debug ("Process %d exited with return value %d", pid, WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        g_debug ("Process %d terminated with signal %d", pid, WTERMSIG (status));

    if (process->priv->quit_timeout)
        g_source_remove (process->priv->quit_timeout);
    process->priv->quit_timeout = 0;  
    process->priv->pid = 0;
    g_hash_table_remove (processes, GINT_TO_POINTER (pid));

    g_signal_emit (process, signals[STOPPED], 0);
}

gboolean
process_start (Process *process, gboolean block)
{
    gint argc;
    gchar **argv;
    gchar **env_keys, **env_values;
    guint i, env_length;
    GList *keys, *link;
    pid_t pid;
    int log_fd = -1;
    GError *error = NULL;

    g_return_val_if_fail (process != NULL, FALSE);
    g_return_val_if_fail (process->priv->command != NULL, FALSE);  
    g_return_val_if_fail (process->priv->pid == 0, FALSE);

    if (!g_shell_parse_argv (process->priv->command, &argc, &argv, &error))
    {
        g_warning ("Error parsing command %s: %s", process->priv->command, error->message);
        return FALSE;
    }

    if (process->priv->log_file)
        log_fd = log_file_open (process->priv->log_file, process->priv->log_mode);

    /* Work out variables to set */
    env_length = g_hash_table_size (process->priv->env);
    env_keys = g_malloc (sizeof (gchar *) * env_length);
    env_values = g_malloc (sizeof (gchar *) * env_length);
    keys = g_hash_table_get_keys (process->priv->env);
    for (i = 0, link = keys; i < env_length; i++, link = link->next)
    {
        env_keys[i] = link->data;
        env_values[i] = g_hash_table_lookup (process->priv->env, env_keys[i]);
    }
    g_list_free (keys);

    pid = fork ();
    if (pid == 0)
    {
        /* Do custom setup */
        if (process->priv->run_func)
            process->priv->run_func (process, process->priv->run_func_data);

        /* Redirect output to logfile */
        if (log_fd >= 0)
        {
             if (process->priv->log_stdout)
                 dup2 (log_fd, STDOUT_FILENO);
             dup2 (log_fd, STDERR_FILENO);
             close (log_fd);
        }

        /* Set environment */
        if (process->priv->clear_environment)
#ifdef HAVE_CLEARENV
            clearenv ();
#else
            environ = NULL;
#endif
        for (i = 0; i < env_length; i++)
            setenv (env_keys[i], env_values[i], TRUE);

        /* Reset SIGPIPE handler so the child has default behaviour (we disabled it at LightDM start) */
        signal (SIGPIPE, SIG_DFL);

        execvp (argv[0], argv);
        _exit (EXIT_FAILURE);
    }
  
    close (log_fd);
    g_strfreev (argv);
    g_free (env_keys);
    g_free (env_values);

    if (pid < 0)
    {
        g_warning ("Failed to fork: %s", strerror (errno));
        return FALSE;
    }

    g_debug ("Launching process %d: %s", pid, process->priv->command);

    process->priv->pid = pid;

    if (block)
    {
        int exit_status;
        waitpid (process->priv->pid, &exit_status, 0);
        process_watch_cb (process->priv->pid, exit_status, process);
    }  
    else
    {
        g_hash_table_insert (processes, GINT_TO_POINTER (process->priv->pid), g_object_ref (process));
        process->priv->watch = g_child_watch_add (process->priv->pid, process_watch_cb, process);
    }

    return TRUE;
}

gboolean
process_get_is_running (Process *process)
{
    g_return_val_if_fail (process != NULL, FALSE);
    return process->priv->pid != 0;
}

GPid
process_get_pid (Process *process)
{
    g_return_val_if_fail (process != NULL, 0);
    return process->priv->pid;
}

void
process_signal (Process *process, int signum)
{
    g_return_if_fail (process != NULL);

    if (process->priv->pid == 0)
        return;

    g_debug ("Sending signal %d to process %d", signum, process->priv->pid);

    if (kill (process->priv->pid, signum) < 0)
    {
        /* Ignore ESRCH, we will pick that up in our wait */
        if (errno != ESRCH)
            g_warning ("Error sending signal %d to process %d: %s", signum, process->priv->pid, strerror (errno));
    }
}

static gboolean
quit_timeout_cb (Process *process)
{
    process->priv->quit_timeout = 0;
    process_signal (process, SIGKILL);
    return FALSE;
}

void
process_stop (Process *process)
{
    g_return_if_fail (process != NULL);

    if (process->priv->stopping)
        return;
    process->priv->stopping = TRUE;

    /* If already stopped then we're done! */
    if (process->priv->pid == 0)
        return;

    /* Send SIGTERM, and then SIGKILL if no response */
    process->priv->quit_timeout = g_timeout_add (5000, (GSourceFunc) quit_timeout_cb, process);
    process_signal (process, SIGTERM);
}

int
process_get_exit_status (Process *process)
{
    g_return_val_if_fail (process != NULL, -1);
    return process->priv->exit_status;
}

static void
process_init (Process *process)
{
    process->priv = G_TYPE_INSTANCE_GET_PRIVATE (process, PROCESS_TYPE, ProcessPrivate);
    process->priv->env = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
process_stopped (Process *process)
{
}

static void
process_finalize (GObject *object)
{
    Process *self;

    self = PROCESS (object);

    if (self->priv->pid > 0)
        g_hash_table_remove (processes, GINT_TO_POINTER (self->priv->pid));

    g_free (self->priv->log_file);
    g_free (self->priv->command);
    g_hash_table_unref (self->priv->env);
    if (self->priv->quit_timeout)
        g_source_remove (self->priv->quit_timeout);
    if (self->priv->watch)
        g_source_remove (self->priv->watch);

    if (self->priv->pid)
        kill (self->priv->pid, SIGTERM);

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
    int signo;
    pid_t pid;
    Process *process;

    errno = 0;
    if (read (signal_pipe[0], &signo, sizeof (int)) != sizeof (int) || 
        read (signal_pipe[0], &pid, sizeof (pid_t)) != sizeof (pid_t))
    {
        g_warning ("Error reading from signal pipe: %s", strerror (errno));
        return FALSE;
    }

    g_debug ("Got signal %d from process %d", signo, pid);

    process = g_hash_table_lookup (processes, GINT_TO_POINTER (pid));
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

    g_type_class_add_private (klass, sizeof (ProcessPrivate));

    signals[GOT_DATA] =
        g_signal_new ("got-data",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (ProcessClass, got_data),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0); 
    signals[GOT_SIGNAL] =
        g_signal_new ("got-signal",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (ProcessClass, got_signal),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);
    signals[STOPPED] =
        g_signal_new ("stopped",
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
    action.sa_flags = SA_SIGINFO;
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGHUP, &action, NULL);
    sigaction (SIGUSR1, &action, NULL);
    sigaction (SIGUSR2, &action, NULL);
}
