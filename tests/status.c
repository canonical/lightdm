#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
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
    char status[1024];
    va_list ap;
  
    va_start (ap, format);
    vsnprintf (status, 1024, format, ap);
    va_end (ap);

    s = socket (AF_UNIX, SOCK_DGRAM, 0);
    if (s < 0)
        return;
    address.sun_family = AF_UNIX;
    strncpy (address.sun_path, ".status-socket", UNIX_PATH_MAX);
    sendto (s, status, strlen (status), 0, (struct sockaddr *) &address, sizeof (address));
    close (s);
}
