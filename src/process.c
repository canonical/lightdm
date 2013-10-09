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
#include <glib/gstdio.h>
#include <config.h>

#include "process.h"

enum {
    RUN,
    GOT_DATA,
    GOT_SIGNAL,  
    STOPPED,
    LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

struct ProcessPrivate
{  
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

    /* Timeout waiting for process to quit */
    guint quit_timeout;

    /* Watch on process */
    guint watch;
};

G_DEFINE_TYPE (Process, process, G_TYPE_OBJECT);

static Process *current_process = NULL;
static GHashTable *processes = NULL;
static int signal_pipe[2];

Process *
process_get_current (void)
{
    if (current_process)
        return current_process;

    current_process = process_new ();
    current_process->priv->pid = getpid ();

    return current_process;
}

Process *
process_new (void)
{
    return g_object_new (PROCESS_TYPE, NULL);
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

    process->priv->exit_status = status;

    if (WIFEXITED (status))
        g_debug ("Process %d exited with return value %d", pid, WEXITSTATUS (status));
    else if (WIFSIGNALED (status))
        g_debug ("Process %d terminated with signal %d", pid, WTERMSIG (status));

    if (process->priv->watch)
        g_source_remove (process->priv->watch);
    process->priv->watch = 0;

    if (process->priv->quit_timeout)
        g_source_remove (process->priv->quit_timeout);
    process->priv->quit_timeout = 0;  
    process->priv->pid = 0;
    g_hash_table_remove (processes, GINT_TO_POINTER (pid));

    g_signal_emit (process, signals[STOPPED], 0);
}

static void
process_run (Process *process)
{
    gint argc;
    gchar **argv;
    GHashTableIter iter;
    gpointer key, value;
    GError *error = NULL;

    if (!g_shell_parse_argv (process->priv->command, &argc, &argv, &error))
    {
        g_warning ("Error parsing command %s: %s", process->priv->command, error->message);
        _exit (EXIT_FAILURE);
    }

    if (process->priv->clear_environment)
#ifdef HAVE_CLEARENV
        clearenv ();
#else
        environ = NULL;
#endif

    g_hash_table_iter_init (&iter, process->priv->env);
    while (g_hash_table_iter_next (&iter, &key, &value))
        g_setenv ((gchar *)key, (gchar *)value, TRUE);
  
    execvp (argv[0], argv);

    g_warning ("Error executing child process %s: %s", argv[0], g_strerror (errno));
    _exit (EXIT_FAILURE);
}

gboolean
process_start (Process *process, gboolean block)
{
    pid_t pid;

    g_return_val_if_fail (process != NULL, FALSE);
    g_return_val_if_fail (process->priv->command != NULL, FALSE);  
    g_return_val_if_fail (process->priv->pid == 0, FALSE);

    pid = fork ();
    if (pid < 0)
    {
        g_warning ("Failed to fork: %s", strerror (errno));
        return FALSE;
    }

    if (pid == 0)
        g_signal_emit (process, signals[RUN], 0);

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
        g_warning ("Error sending signal %d to process %d: %s", signum, process->priv->pid, strerror (errno));
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
    /* NOTE: Using g_printerr as can't call g_warning from a signal callback */
    if (write (signal_pipe[1], &info->si_signo, sizeof (int)) < 0 ||
        write (signal_pipe[1], &info->si_pid, sizeof (pid_t)) < 0)
        g_printerr ("Failed to write to signal pipe: %s", strerror (errno));
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
        return TRUE;
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

    klass->run = process_run;
    klass->stopped = process_stopped;
    object_class->finalize = process_finalize;  

    g_type_class_add_private (klass, sizeof (ProcessPrivate));

    signals[RUN] =
        g_signal_new ("run",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (ProcessClass, run),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0); 
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
