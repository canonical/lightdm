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

#ifndef _CHILD_PROCESS_H_
#define _CHILD_PROCESS_H_

#include <glib-object.h>
#include "child-process.h"
#include "user.h"

G_BEGIN_DECLS

#define CHILD_PROCESS_TYPE (child_process_get_type())
#define CHILD_PROCESS_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), CHILD_PROCESS_TYPE, ChildProcessClass))
#define CHILD_PROCESS(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), CHILD_PROCESS_TYPE, ChildProcess))

typedef struct ChildProcessPrivate ChildProcessPrivate;

typedef struct
{
    GObject              parent_instance;
    ChildProcessPrivate *priv;
} ChildProcess;

typedef struct
{
    GObjectClass parent_class;
    void (*got_data)(ChildProcess *process);
    void (*got_signal)(ChildProcess *process, int signum);
    void (*exited)(ChildProcess *process, int status);
    void (*terminated) (ChildProcess *process, int signum);
} ChildProcessClass;

GType child_process_get_type (void);

ChildProcess *child_process_get_parent (void);

ChildProcess *child_process_new (void);

void child_process_set_log_file (ChildProcess *process, const gchar *log_file);

const gchar *child_process_get_log_file (ChildProcess *process);

void child_process_set_env (ChildProcess *process, const gchar *name, const gchar *value);

gboolean child_process_start (ChildProcess *process,
                              User *user,
                              const gchar *working_dir,
                              const gchar *command,
                              gboolean create_pipe, // FIXME: Move the pipe code into session.c, and then make a whitelist of fds to keep open
                              GError **error);

GPid child_process_get_pid (ChildProcess *process);

void child_process_signal (ChildProcess *process, int signum);

GIOChannel *child_process_get_to_child_channel (ChildProcess *process);

GIOChannel *child_process_get_from_child_channel (ChildProcess *process);

void child_process_stop_all (void);

G_END_DECLS

#endif /* _CHILD_PROCESS_H_ */
