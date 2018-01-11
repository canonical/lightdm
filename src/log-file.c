/*
 * Copyright (C) 2015 Alexandros Frantzis
 * Author: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include "log-file.h"

int
log_file_open (const gchar *log_filename, LogMode log_mode)
{
    int open_flags = O_WRONLY | O_CREAT;
    if (log_mode == LOG_MODE_BACKUP_AND_TRUNCATE)
    {
        /* Move old file out of the way */
        g_autofree gchar *old_filename = NULL;

        old_filename = g_strdup_printf ("%s.old", log_filename);
        rename (log_filename, old_filename);

        open_flags |= O_TRUNC;
    }
    else if (log_mode == LOG_MODE_APPEND)
    {
        /* Keep appending to it */
        open_flags |= O_APPEND;
    }
    else
    {
        g_warning ("Failed to open log file %s: invalid log mode %d specified",
                   log_filename, log_mode);
        return -1;
    }

    /* Open file and log to it */
    int log_fd = open (log_filename, open_flags, 0600);
    if (log_fd < 0)
        g_warning ("Failed to open log file %s: %s", log_filename, g_strerror (errno));

    return log_fd;
}
