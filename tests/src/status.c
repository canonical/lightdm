#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "status.h"

/* For some reason sys/un.h doesn't define this */
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

void
notify_status (const char *format, ...)
{
    int s;
    struct sockaddr_un address;
    char *socket_name, status[1024];
    va_list ap;
  
    va_start (ap, format);
    vsnprintf (status, 1024, format, ap);
    va_end (ap);

    s = socket (AF_UNIX, SOCK_DGRAM, 0);
    if (s < 0)
    {
        fprintf (stderr, "Unable to open socket for status: %s\n", strerror (errno));
        return;
    }

    socket_name = getenv ("LIGHTDM_TEST_STATUS_SOCKET");
    if (!socket_name)
    {
        fprintf (stderr, "LIGHTDM_TEST_STATUS_SOCKET not defined\n");
        fprintf (stderr, "%s", status);
        fprintf (stderr, "\n");
        return;
    }

    address.sun_family = AF_UNIX;
    strncpy (address.sun_path, socket_name, UNIX_PATH_MAX);
    if (sendto (s, status, strlen (status), 0, (struct sockaddr *) &address, sizeof (address)) < 0)
    {
        fprintf (stderr, "Error writing status: %s\n", strerror (errno));
        return;
    }

    close (s);
}
