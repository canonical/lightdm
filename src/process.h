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

#ifndef PROCESS_H_
#define PROCESS_H_

#include <glib-object.h>

#include "log-file.h"

G_BEGIN_DECLS

#define PROCESS_TYPE (process_get_type())
#define PROCESS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), PROCESS_TYPE, ProcessClass))
#define PROCESS(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PROCESS_TYPE, Process))
#define PROCESS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), PROCESS_TYPE, ProcessClass))

#define PROCESS_SIGNAL_GOT_DATA   "got-data"
#define PROCESS_SIGNAL_GOT_SIGNAL "got-signal"
#define PROCESS_SIGNAL_STOPPED    "stopped"

typedef struct ProcessPrivate ProcessPrivate;

typedef struct
{
    GObject              parent_instance;
    ProcessPrivate *priv;
} Process;

typedef struct
{
    GObjectClass parent_class;
    void (*got_data)(Process *process);
    void (*got_signal)(Process *process, int signum);
    void (*stopped)(Process *process);
} ProcessClass;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Process, g_object_unref);

typedef void (*ProcessRunFunc)(Process *process, gpointer user_data);

GType process_get_type (void);

Process *process_get_current (void);

Process *process_new (ProcessRunFunc run_func, gpointer run_func_data);

void process_set_log_file (Process *process, const gchar *path, gboolean log_stdout, LogMode log_mode);

void process_set_clear_environment (Process *process, gboolean clear_environment);

gboolean process_get_clear_environment (Process *process);

void process_set_env (Process *process, const gchar *name, const gchar *value);

const gchar *process_get_env (Process *process, const gchar *name);

void process_set_command (Process *process, const gchar *command);

const gchar *process_get_command (Process *process);

gboolean process_start (Process *process, gboolean block);

gboolean process_get_is_running (Process *process);

GPid process_get_pid (Process *process);

void process_signal (Process *process, int signum);

void process_stop (Process *process);

int process_get_exit_status (Process *process);

G_END_DECLS

#endif /* PROCESS_H_ */
