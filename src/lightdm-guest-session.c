/* -*- Mode: C; indent-tabs-mode: nil; tab-width: 4 -*-
 *
 * Copyright (C) 2011 Canonical Ltd.
 * Author: Martin Pitt <martin.pitt@ubuntu.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

/* This is a simple wrapper which just re-execve()'s the program given as its
 * arguments. This allows MAC systems like AppArmor or SELinux to apply a
 * policy on this wrapper which applies to guest sessions only. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int
main (int argc, char *argv[], char *envp[])
{
    if (argc < 2)
    {
        fprintf (stderr, "Usage: %s COMMAND [ARGS]\n", argv[0]);
        return EXIT_FAILURE;
    }

    execve (argv[1], argv+1, envp);

    fprintf (stderr, "Failed to run guest session '%s': %s\n", argv[1], strerror (errno));

    return EXIT_FAILURE;
}
