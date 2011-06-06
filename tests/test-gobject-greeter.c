#include <stdio.h>
#include <lightdm/greeter.h>

static void connected_cb (LdmGreeter *greeter)
{
    printf ("CONNECTED\n");
}

int main (int argc, char **argv)
{
    LdmGreeter *greeter;
  
    g_type_init ();

    greeter = ldm_greeter_new ();
    g_object_connect (greeter, "connected", G_CALLBACK (connected_cb), NULL);
    ldm_greeter_connect_to_server (greeter);

    g_main_loop_run (g_main_loop_new (NULL, FALSE));

    return 0;
}
