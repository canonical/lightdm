#include <stdlib.h>
#include <unistd.h>

#include "status.h"

int
main (int argc, char **argv)
{
    gchar *display;

    status_connect (NULL, NULL);

    display = getenv ("DISPLAY");
    if (display == NULL)
        status_notify ("GREETER-WRAPPER-? START");
    else if (display[0] == ':')
        status_notify ("GREETER-WRAPPER-X-%s START", display + 1);
    else
        status_notify ("GREETER-WRAPPER-X-%s START", display);

    execv (argv[1], argv + 1);

    return EXIT_FAILURE;
}
