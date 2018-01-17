#define _GNU_SOURCE
#define __USE_GNU

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <pwd.h>
#include <unistd.h>
#include <dirent.h>
#include <grp.h>
#include <security/pam_appl.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <utmp.h>
#include <utmpx.h>
#ifdef __linux__
#include <linux/vt.h>
#endif
#include <glib.h>
#include <xcb/xcb.h>
#include <gio/gunixsocketaddress.h>

#if HAVE_LIBAUDIT
#include <libaudit.h>
#endif

#include "status.h"

#define LOGIN_PROMPT "login:"

static int tty_fd = -1;

static GList *user_entries = NULL;
static GList *getpwent_link = NULL;

static GList *group_entries = NULL;

static int active_vt = 7;

static gboolean status_connected = FALSE;
static GKeyFile *config;

static void connect_status (void)
{
    if (status_connected)
        return;
    status_connected = TRUE;

    status_connect (NULL, NULL);

    config = g_key_file_new ();
    g_key_file_load_from_file (config, g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "script", NULL), G_KEY_FILE_NONE, NULL);
}

struct pam_handle
{
    char *id;
    char *service_name;
    char *user;
    char *authtok;
    char *ruser;
    char *tty;
    char **envlist;
    struct pam_conv conversation;
};

int
gethostname (char *name, size_t len)
{
   snprintf (name, len, "lightdm-test");
   return 0;
}

uid_t
getuid (void)
{
    return 0;
}

/*uid_t
geteuid (void)
{
    return 0;
}*/

int
initgroups (const char *user, gid_t group)
{
    gid_t g[1];

    g[0] = group;
    setgroups (1, g);

    return 0;
}

int
getgroups (int size, gid_t list[])
{
    /* Get groups we are a member of */
    const gchar *group_list = g_getenv ("LIGHTDM_TEST_GROUPS");
    if (!group_list)
        group_list = "";
    g_auto(GStrv) groups = g_strsplit (group_list, ",", -1);
    gint groups_length = g_strv_length (groups);

    if (size != 0)
    {
        if (groups_length > size)
        {
            errno = EINVAL;
            return -1;
        }
        for (int i = 0; groups[i]; i++)
            list[i] = atoi (groups[i]);
    }

    return groups_length;
}

int
setgroups (size_t size, const gid_t *list)
{
    g_autoptr(GString) group_list = g_string_new ("");
    for (size_t i = 0; i < size; i++)
    {
        if (i != 0)
            g_string_append (group_list, ",");
        g_string_append_printf (group_list, "%d", list[i]);
    }
    g_setenv ("LIGHTDM_TEST_GROUPS", group_list->str, TRUE);

    return 0;
}

int
setgid (gid_t gid)
{
    return 0;
}

int
setegid (gid_t gid)
{
    return 0;
}

int
setresgid (gid_t rgid, gid_t ugid, gid_t sgid)
{
    return 0;
}

int
setuid (uid_t uid)
{
    return 0;
}

int
seteuid (uid_t uid)
{
    return 0;
}

int
setresuid (uid_t ruid, uid_t uuid, uid_t suid)
{
    return 0;
}

static gchar *
redirect_path (const gchar *path)
{
    // Don't redirect if inside the running directory
    if (g_str_has_prefix (path, g_getenv ("LIGHTDM_TEST_ROOT")))
        return g_strdup (path);

    if (g_str_has_prefix (path, SYSCONFDIR))
        return g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "etc", path + strlen (SYSCONFDIR), NULL);

    if (g_str_has_prefix (path, LOCALSTATEDIR))
        return g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "var", path + strlen (LOCALSTATEDIR), NULL);

    if (g_str_has_prefix (path, DATADIR))
        return g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "usr", "share", path + strlen (DATADIR), NULL);

    // Don't redirect if inside the build directory
    if (g_str_has_prefix (path, BUILDDIR))
        return g_strdup (path);

    if (g_str_has_prefix (path, "/tmp"))
        return g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "tmp", path + strlen ("/tmp"), NULL);

    if (g_str_has_prefix (path, "/run"))
        return g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "run", path + strlen ("/run"), NULL);

    if (g_str_has_prefix (path, "/etc/xdg"))
        return g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "etc", "xdg", path + strlen ("/etc/xdg"), NULL);

    if (g_str_has_prefix (path, "/usr/share/lightdm"))
        return g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "usr", "share", "lightdm", path + strlen ("/usr/share/lightdm"), NULL);

    return g_strdup (path);
}

#ifdef __linux__
static int
open_wrapper (const char *func, const char *pathname, int flags, mode_t mode)
{
    int (*_open) (const char *pathname, int flags, mode_t mode) = dlsym (RTLD_NEXT, func);

    if (strcmp (pathname, "/dev/tty0") == 0)
    {
        if (tty_fd < 0)
        {
            tty_fd = _open ("/dev/null", flags, mode);
            fcntl (tty_fd, F_SETFD, FD_CLOEXEC);
        }
        return tty_fd;
    }

    g_autofree gchar *new_path = redirect_path (pathname);
    return _open (new_path, flags, mode);
}

int
open (const char *pathname, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list ap;
        va_start (ap, flags);
        mode = va_arg (ap, mode_t);
        va_end (ap);
    }
    return open_wrapper ("open", pathname, flags, mode);
}

int
open64 (const char *pathname, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list ap;
        va_start (ap, flags);
        mode = va_arg (ap, mode_t);
        va_end (ap);
    }
    return open_wrapper ("open64", pathname, flags, mode);
}

FILE *
fopen (const char *path, const char *mode)
{
    FILE *(*_fopen) (const char *pathname, const char *mode) = dlsym (RTLD_NEXT, "fopen");

    g_autofree gchar *new_path = redirect_path (path);
    return _fopen (new_path, mode);
}

int
unlinkat (int dirfd, const char *pathname, int flags)
{
    int (*_unlinkat) (int dirfd, const char *pathname, int flags) = dlsym (RTLD_NEXT, "unlinkat");

    g_autofree gchar *new_path = redirect_path (pathname);
    return _unlinkat (dirfd, new_path, flags);
}

int
creat (const char *pathname, mode_t mode)
{
    int (*_creat) (const char *pathname, mode_t mode) = dlsym (RTLD_NEXT, "creat");

    g_autofree gchar *new_path = redirect_path (pathname);
    return _creat (new_path, mode);
}

int
creat64 (const char *pathname, mode_t mode)
{
    int (*_creat64) (const char *pathname, mode_t mode) = dlsym (RTLD_NEXT, "creat64");

    g_autofree gchar *new_path = redirect_path (pathname);
    return _creat64 (new_path, mode);
}

int
access (const char *pathname, int mode)
{
    int (*_access) (const char *pathname, int mode) = dlsym (RTLD_NEXT, "access");

    if (strcmp (pathname, "/dev/tty0") == 0)
        return F_OK;
    if (strcmp (pathname, "/sys/class/tty/tty0/active") == 0)
        return F_OK;

    g_autofree gchar *new_path = redirect_path (pathname);
    return _access (new_path, mode);
}

int
stat (const char *path, struct stat *buf)
{
    int (*_stat) (const char *path, struct stat *buf) = dlsym (RTLD_NEXT, "stat");

    g_autofree gchar *new_path = redirect_path (path);
    return _stat (new_path, buf);
}

int
stat64 (const char *path, struct stat64 *buf)
{
    int (*_stat64) (const char *path, struct stat64 *buf) = dlsym (RTLD_NEXT, "stat64");

    g_autofree gchar *new_path = redirect_path (path);
    return _stat64 (new_path, buf);
}

int
__xstat (int version, const char *path, struct stat *buf)
{
    int (*___xstat) (int version, const char *path, struct stat *buf) = dlsym (RTLD_NEXT, "__xstat");

    g_autofree gchar *new_path = redirect_path (path);
    return ___xstat (version, new_path, buf);
}

int
__xstat64 (int version, const char *path, struct stat64 *buf)
{
    int (*___xstat64) (int version, const char *path, struct stat64 *buf) = dlsym (RTLD_NEXT, "__xstat64");

    g_autofree gchar *new_path = redirect_path (path);
    return ___xstat64 (version, new_path, buf);
}

int
__fxstatat(int ver, int dirfd, const char *pathname, struct stat *buf, int flags)
{
    int (*___fxstatat) (int ver, int dirfd, const char *pathname, struct stat *buf, int flags) = dlsym (RTLD_NEXT, "__fxstatat");

    g_autofree gchar *new_path = redirect_path (pathname);
    return ___fxstatat (ver, dirfd, new_path, buf, flags);
}

int
__fxstatat64(int ver, int dirfd, const char *pathname, struct stat64 *buf, int flags)
{
    int (*___fxstatat64) (int ver, int dirfd, const char *pathname, struct stat64 *buf, int flags) = dlsym (RTLD_NEXT, "__fxstatat64");

    g_autofree gchar *new_path = redirect_path (pathname);
    return ___fxstatat64 (ver, dirfd, new_path, buf, flags);
}

DIR *
opendir (const char *name)
{
    DIR *(*_opendir) (const char *name) = dlsym (RTLD_NEXT, "opendir");

    g_autofree gchar *new_path = redirect_path (name);
    return _opendir (new_path);
}

int
mkdir (const char *pathname, mode_t mode)
{
    int (*_mkdir) (const char *pathname, mode_t mode) = dlsym (RTLD_NEXT, "mkdir");

    g_autofree gchar *new_path = redirect_path (pathname);
    return _mkdir (new_path, mode);
}

int
chown (const char *pathname, uid_t owner, gid_t group)
{
    /* Just fake it - we're not root */
    return 0;
}

int
chmod (const char *path, mode_t mode)
{
    int (*_chmod) (const char *path, mode_t mode) = dlsym (RTLD_NEXT, "chmod");

    g_autofree gchar *new_path = redirect_path (path);
    return _chmod (new_path, mode);
}

int
ioctl (int d, unsigned long request, ...)
{
    int (*_ioctl) (int d, int request, ...) = dlsym (RTLD_NEXT, "ioctl");

    if (d > 0 && d == tty_fd)
    {
        va_list ap;
        switch (request)
        {
        case VT_GETSTATE:
            va_start (ap, request);
            struct vt_stat *vt_state = va_arg (ap, struct vt_stat *);
            va_end (ap);
            vt_state->v_active = active_vt;
            break;
        case VT_ACTIVATE:
            va_start (ap, request);
            int vt = va_arg (ap, int);
            va_end (ap);
            if (vt != active_vt)
            {
                active_vt = vt;
                connect_status ();
                status_notify ("VT ACTIVATE VT=%d", active_vt);
            }
            break;
        case VT_WAITACTIVE:
            break;
        }
        return 0;
    }
    else
    {
        va_list ap;

        va_start (ap, request);
        void *data = va_arg (ap, void *);
        va_end (ap);
        return _ioctl (d, request, data);
    }
}

static void
add_port_redirect (int requested_port, int redirected_port)
{
    g_autoptr(GKeyFile) file = g_key_file_new ();
    g_autofree gchar *path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), ".port-redirects", NULL);
    g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, NULL);

    g_autofree gchar *name = g_strdup_printf ("%d", requested_port);
    g_key_file_set_integer (file, name, "redirected", redirected_port);

    g_autofree gchar *data = g_key_file_to_data (file, NULL, NULL);
    g_file_set_contents (path, data, -1, NULL);
}

static int
find_port_redirect (int port)
{
    g_autoptr(GKeyFile) file = g_key_file_new ();
    g_autofree gchar *path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), ".port-redirects", NULL);
    g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, NULL);

    g_autofree gchar *name = g_strdup_printf ("%d", port);
    return g_key_file_get_integer (file, name, "redirected", NULL);
}

int
bind (int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int (*_bind) (int sockfd, const struct sockaddr *addr, socklen_t addrlen) = dlsym (RTLD_NEXT, "bind");

    const struct sockaddr *modified_addr = addr;
    struct sockaddr_in temp_addr_in;
    struct sockaddr_in6 temp_addr_in6;
    struct sockaddr_un temp_addr_un;
    int port = 0, redirected_port = 0;
    const char *path;
    switch (addr->sa_family)
    {
    case AF_UNIX:
        path = ((const struct sockaddr_un *) addr)->sun_path;
        if (path[0] != '\0')
        {
            g_autofree gchar *new_path = redirect_path (path);
            memcpy (&temp_addr_un, addr, sizeof (struct sockaddr_un));
            strncpy (temp_addr_un.sun_path, new_path, sizeof (temp_addr_un.sun_path) - 1);
            modified_addr = (struct sockaddr *) &temp_addr_un;
        }
        break;
    case AF_INET:
        port = ntohs (((const struct sockaddr_in *) addr)->sin_port);
        redirected_port = find_port_redirect (port);
        memcpy (&temp_addr_in, addr, sizeof (struct sockaddr_in));
        modified_addr = (struct sockaddr *) &temp_addr_in;
        if (redirected_port != 0)
            temp_addr_in.sin_port = htons (redirected_port);
        else
            temp_addr_in.sin_port = 0;
        break;
    case AF_INET6:
        port = ntohs (((const struct sockaddr_in6 *) addr)->sin6_port);
        redirected_port = find_port_redirect (port);
        memcpy (&temp_addr_in6, addr, sizeof (struct sockaddr_in6));
        modified_addr = (struct sockaddr *) &temp_addr_in6;
        if (redirected_port != 0)
            temp_addr_in6.sin6_port = htons (redirected_port);
        else
            temp_addr_in6.sin6_port = 0;
        break;
    }

    int retval = _bind (sockfd, modified_addr, addrlen);

    socklen_t temp_addr_len;
    switch (addr->sa_family)
    {
    case AF_INET:
        temp_addr_len = sizeof (temp_addr_in);
        getsockname (sockfd, &temp_addr_in, &temp_addr_len);
        if (redirected_port == 0)
        {
            redirected_port = ntohs (temp_addr_in.sin_port);
            add_port_redirect (port, redirected_port);
        }
        break;
    case AF_INET6:
        temp_addr_len = sizeof (temp_addr_in6);
        getsockname (sockfd, &temp_addr_in6, &temp_addr_len);
        if (redirected_port == 0)
        {
            redirected_port = ntohs (temp_addr_in6.sin6_port);
            add_port_redirect (port, redirected_port);
        }
        break;
    }

    return retval;
}

#include <ctype.h>

int
connect (int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    int (*_connect) (int sockfd, const struct sockaddr *addr, socklen_t addrlen) = dlsym (RTLD_NEXT, "connect");

    const struct sockaddr *modified_addr = addr;
    struct sockaddr_in temp_addr_in;
    struct sockaddr_in6 temp_addr_in6;
    struct sockaddr_un temp_addr_un;
    int port = 0, redirected_port = 0;
    const char *path;
    switch (addr->sa_family)
    {
    case AF_UNIX:
        path = ((const struct sockaddr_un *) addr)->sun_path;
        if (path[0] != '\0')
        {
            g_autofree gchar *new_path = redirect_path (path);
            memcpy (&temp_addr_un, addr, addrlen);
            strncpy (temp_addr_un.sun_path, new_path, sizeof (temp_addr_un.sun_path) - 1);
            modified_addr = (struct sockaddr *) &temp_addr_un;
        }
        break;
    case AF_INET:
        port = ntohs (((const struct sockaddr_in *) addr)->sin_port);
        redirected_port = find_port_redirect (port);
        if (redirected_port != 0)
        {
            memcpy (&temp_addr_in, addr, sizeof (struct sockaddr_in));
            temp_addr_in.sin_port = htons (redirected_port);
            modified_addr = (struct sockaddr *) &temp_addr_in;
        }
        break;
    case AF_INET6:
        port = ntohs (((const struct sockaddr_in6 *) addr)->sin6_port);
        redirected_port = find_port_redirect (port);
        if (redirected_port != 0)
        {
            memcpy (&temp_addr_in6, addr, sizeof (struct sockaddr_in6));
            temp_addr_in6.sin6_port = htons (redirected_port);
            modified_addr = (struct sockaddr *) &temp_addr_in6;
        }
        break;
    }

    return _connect (sockfd, modified_addr, addrlen);
}

ssize_t
sendto (int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
    ssize_t (*_sendto) (int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) = dlsym (RTLD_NEXT, "sendto");

    int port, redirected_port;
    const char *path;
    const struct sockaddr *modified_addr = dest_addr;
    struct sockaddr_in temp_addr_in;
    struct sockaddr_in6 temp_addr_in6;
    struct sockaddr_un temp_addr_un;
    switch (dest_addr->sa_family)
    {
    case AF_UNIX:
        path = ((const struct sockaddr_un *) dest_addr)->sun_path;
        if (path[0] != '\0')
        {
            g_autofree gchar *new_path = redirect_path (path);
            memcpy (&temp_addr_un, dest_addr, sizeof (struct sockaddr_un));
            strncpy (temp_addr_un.sun_path, new_path, sizeof (temp_addr_un.sun_path) - 1);
            modified_addr = (struct sockaddr *) &temp_addr_un;
        }
        break;
    case AF_INET:
        port = ntohs (((const struct sockaddr_in *) dest_addr)->sin_port);
        redirected_port = find_port_redirect (port);
        if (redirected_port != 0)
        {
            memcpy (&temp_addr_in, dest_addr, sizeof (struct sockaddr_in));
            temp_addr_in.sin_port = htons (redirected_port);
            modified_addr = (struct sockaddr *) &temp_addr_in;
        }
        break;
    case AF_INET6:
        port = ntohs (((const struct sockaddr_in6 *) dest_addr)->sin6_port);
        redirected_port = find_port_redirect (port);
        if (redirected_port != 0)
        {
            memcpy (&temp_addr_in6, dest_addr, sizeof (struct sockaddr_in6));
            temp_addr_in6.sin6_port = htons (redirected_port);
            modified_addr = (struct sockaddr *) &temp_addr_in6;
        }
        break;
    }

    return _sendto (sockfd, buf, len, flags, modified_addr, addrlen);
}

int
close (int fd)
{
    if (fd > 0 && fd == tty_fd)
        return 0;

    int (*_close) (int fd) = dlsym (RTLD_NEXT, "close");
    return _close (fd);
}
#endif

static void
free_user (gpointer data)
{
    struct passwd *entry = data;

    g_free (entry->pw_name);
    g_free (entry->pw_passwd);
    g_free (entry->pw_gecos);
    g_free (entry->pw_dir);
    g_free (entry->pw_shell);
    g_free (entry);
}

static void
load_passwd_file (void)
{
    g_list_free_full (user_entries, free_user);
    user_entries = NULL;
    getpwent_link = NULL;

    g_autofree gchar *path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "etc", "passwd", NULL);
    g_autofree gchar *data = NULL;
    g_autoptr(GError) error = NULL;
    if (!g_file_get_contents (path, &data, NULL, &error))
    {
        g_warning ("Error loading passwd file: %s", error->message);
        return;
    }

    g_auto(GStrv) lines = g_strsplit (data, "\n", -1);

    for (gint i = 0; lines[i]; i++)
    {
        const gchar *line = g_strstrip (lines[i]);
        g_auto(GStrv) fields = g_strsplit (line, ":", -1);
        if (g_strv_length (fields) == 7)
        {
            struct passwd *entry = malloc (sizeof (struct passwd));

            entry->pw_name = g_strdup (fields[0]);
            entry->pw_passwd = g_strdup (fields[1]);
            entry->pw_uid = atoi (fields[2]);
            entry->pw_gid = atoi (fields[3]);
            entry->pw_gecos = g_strdup (fields[4]);
            entry->pw_dir = g_strdup (fields[5]);
            entry->pw_shell = g_strdup (fields[6]);
            user_entries = g_list_append (user_entries, entry);
        }
    }
}

struct passwd *
getpwent (void)
{
    if (getpwent_link == NULL)
    {
        load_passwd_file ();
        if (user_entries == NULL)
            return NULL;
        getpwent_link = user_entries;
    }
    else
    {
        if (getpwent_link->next == NULL)
            return NULL;
        getpwent_link = getpwent_link->next;
    }

    return getpwent_link->data;
}

void
setpwent (void)
{
    getpwent_link = NULL;
}

void
endpwent (void)
{
    getpwent_link = NULL;
}

struct passwd *
getpwnam (const char *name)
{
    load_passwd_file ();

    for (GList *link = user_entries; link; link = link->next)
    {
        struct passwd *entry = link->data;
        if (strcmp (entry->pw_name, name) == 0)
            return entry;
    }

    return NULL;
}

struct passwd *
getpwuid (uid_t uid)
{
    load_passwd_file ();

    for (GList *link = user_entries; link; link = link->next)
    {
        struct passwd *entry = link->data;
        if (entry->pw_uid == uid)
            return entry;
    }

    return NULL;
}

static void
free_group (gpointer data)
{
    struct group *entry = data;

    g_free (entry->gr_name);
    g_free (entry->gr_passwd);
    g_strfreev (entry->gr_mem);
    g_free (entry);
}

static void
load_group_file (void)
{
    g_list_free_full (group_entries, free_group);
    group_entries = NULL;

    g_autofree gchar *path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "etc", "group", NULL);
    g_autofree gchar *data = NULL;
    g_autoptr(GError) error = NULL;
    if (!g_file_get_contents (path, &data, NULL, &error))
    {
        g_warning ("Error loading group file: %s", error->message);
        return;
    }

    g_auto(GStrv) lines = g_strsplit (data, "\n", -1);

    for (gint i = 0; lines[i]; i++)
    {
        const gchar *line = g_strstrip (lines[i]);
        g_auto(GStrv) fields = g_strsplit (line, ":", -1);
        if (g_strv_length (fields) == 4)
        {
            struct group *entry = malloc (sizeof (struct group));

            entry->gr_name = g_strdup (fields[0]);
            entry->gr_passwd = g_strdup (fields[1]);
            entry->gr_gid = atoi (fields[2]);
            entry->gr_mem = g_strsplit (fields[3], ",", -1);
            group_entries = g_list_append (group_entries, entry);
        }
    }
}

struct group *
getgrnam (const char *name)
{
    load_group_file ();

    for (GList *link = group_entries; link; link = link->next)
    {
        struct group *entry = link->data;
        if (strcmp (entry->gr_name, name) == 0)
            return entry;
    }

    return NULL;
}

struct group *
getgrgid (gid_t gid)
{
    load_group_file ();

    for (GList *link = group_entries; link; link = link->next)
    {
        struct group *entry = link->data;
        if (entry->gr_gid == gid)
            return entry;
    }

    return NULL;
}

int
pam_start (const char *service_name, const char *user, const struct pam_conv *conversation, pam_handle_t **pamh)
{
    pam_handle_t *handle = *pamh = malloc (sizeof (pam_handle_t));
    if (handle == NULL)
        return PAM_BUF_ERR;

    if (user)
        handle->id = g_strdup_printf ("PAM-%s", user);
    else
        handle->id = g_strdup ("PAM");

    connect_status ();
    if (g_key_file_get_boolean (config, "test-pam", "log-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("");
        g_string_append_printf (status, "%s START", handle->id);
        g_string_append_printf (status, " SERVICE=%s", service_name);
        if (user)
            g_string_append_printf (status, " USER=%s", user);
        status_notify ("%s", status->str);
    }

    handle->service_name = strdup (service_name);
    handle->user = user ? strdup (user) : NULL;
    handle->authtok = NULL;
    handle->ruser = NULL;
    handle->tty = NULL;
    handle->conversation.conv = conversation->conv;
    handle->conversation.appdata_ptr = conversation->appdata_ptr;
    handle->envlist = malloc (sizeof (char *) * 1);
    handle->envlist[0] = NULL;

    return PAM_SUCCESS;
}

int
pam_authenticate (pam_handle_t *pamh, int flags)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-pam", "log-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("");
        g_string_append_printf (status, "%s AUTHENTICATE", pamh->id);
        if (flags & PAM_SILENT)
            g_string_append (status, " SILENT");
        if (flags & PAM_DISALLOW_NULL_AUTHTOK)
            g_string_append (status, " DISALLOW_NULL_AUTHTOK");

        status_notify ("%s", status->str);
    }

    gboolean password_matches = FALSE;
    if (strcmp (pamh->service_name, "test-remote") == 0)
    {
        struct pam_message **msg = malloc (sizeof (struct pam_message *) * 1);
        msg[0] = malloc (sizeof (struct pam_message));
        msg[0]->msg_style = PAM_PROMPT_ECHO_ON;
        msg[0]->msg = "remote-login:";
        struct pam_response *resp = NULL;
        int result = pamh->conversation.conv (1, (const struct pam_message **) msg, &resp, pamh->conversation.appdata_ptr);
        free (msg[0]);
        free (msg);
        if (result != PAM_SUCCESS)
            return result;

        if (resp == NULL)
            return PAM_CONV_ERR;
        if (resp[0].resp == NULL)
        {
            free (resp);
            return PAM_CONV_ERR;
        }

        if (pamh->ruser)
            free (pamh->ruser);
        pamh->ruser = strdup (resp[0].resp);
        free (resp[0].resp);
        free (resp);

        msg = malloc (sizeof (struct pam_message *) * 1);
        msg[0] = malloc (sizeof (struct pam_message));
        msg[0]->msg_style = PAM_PROMPT_ECHO_OFF;
        msg[0]->msg = "remote-password:";
        result = pamh->conversation.conv (1, (const struct pam_message **) msg, &resp, pamh->conversation.appdata_ptr);
        free (msg[0]);
        free (msg);
        if (result != PAM_SUCCESS)
            return result;

        if (resp == NULL)
            return PAM_CONV_ERR;
        if (resp[0].resp == NULL)
        {
            free (resp);
            return PAM_CONV_ERR;
        }

        if (pamh->authtok)
            free (pamh->authtok);
        pamh->authtok = strdup (resp[0].resp);
        free (resp[0].resp);
        free (resp);

        password_matches = strcmp (pamh->ruser, "remote-user") == 0 && strcmp (pamh->authtok, "password") == 0;

        if (password_matches)
            return PAM_SUCCESS;
        else
            return PAM_AUTH_ERR;
    }

    /* Prompt for username */
    if (pamh->user == NULL)
    {
        struct pam_message **msg = malloc (sizeof (struct pam_message *) * 1);
        msg[0] = malloc (sizeof (struct pam_message));
        msg[0]->msg_style = PAM_PROMPT_ECHO_ON;
        msg[0]->msg = LOGIN_PROMPT;
        struct pam_response *resp = NULL;
        int result = pamh->conversation.conv (1, (const struct pam_message **) msg, &resp, pamh->conversation.appdata_ptr);
        free (msg[0]);
        free (msg);
        if (result != PAM_SUCCESS)
            return result;

        if (resp == NULL)
            return PAM_CONV_ERR;
        if (resp[0].resp == NULL)
        {
            free (resp);
            return PAM_CONV_ERR;
        }

        pamh->user = strdup (resp[0].resp);
        free (resp[0].resp);
        free (resp);
    }

    /* Crash on authenticate */
    if (strcmp (pamh->user, "crash-authenticate") == 0)
        kill (getpid (), SIGSEGV);

    /* Look up password database */
    struct passwd *entry = getpwnam (pamh->user);

    /* Prompt for password if required */
    if (entry && strcmp (pamh->user, "always-password") != 0 && (strcmp (pamh->service_name, "lightdm-autologin") == 0 || strcmp (entry->pw_passwd, "") == 0))
        password_matches = TRUE;
    else
    {
        struct pam_message **msg = malloc (sizeof (struct pam_message *) * 5);
        int n_messages = 0;
        if (strcmp (pamh->user, "info-prompt") == 0)
        {
            msg[n_messages] = malloc (sizeof (struct pam_message));
            msg[n_messages]->msg_style = PAM_TEXT_INFO;
            msg[n_messages]->msg = "Welcome to LightDM";
            n_messages++;
        }
        if (strcmp (pamh->user, "multi-info-prompt") == 0)
        {
            msg[n_messages] = malloc (sizeof (struct pam_message));
            msg[n_messages]->msg_style = PAM_TEXT_INFO;
            msg[n_messages]->msg = "Welcome to LightDM";
            n_messages++;
            msg[n_messages] = malloc (sizeof (struct pam_message));
            msg[n_messages]->msg_style = PAM_ERROR_MSG;
            msg[n_messages]->msg = "This is an error";
            n_messages++;
            msg[n_messages] = malloc (sizeof (struct pam_message));
            msg[n_messages]->msg_style = PAM_TEXT_INFO;
            msg[n_messages]->msg = "You should have seen three messages";
            n_messages++;
        }
        if (strcmp (pamh->user, "multi-prompt") == 0)
        {
            msg[n_messages] = malloc (sizeof (struct pam_message));
            msg[n_messages]->msg_style = PAM_PROMPT_ECHO_ON;
            msg[n_messages]->msg = "Favorite Color:";
            n_messages++;
        }
        msg[n_messages] = malloc (sizeof (struct pam_message));
        msg[n_messages]->msg_style = PAM_PROMPT_ECHO_OFF;
        msg[n_messages]->msg = "Password:";
        int password_index = n_messages;
        n_messages++;
        struct pam_response *resp = NULL;
        int result = pamh->conversation.conv (n_messages, (const struct pam_message **) msg, &resp, pamh->conversation.appdata_ptr);
        for (int i = 0; i < n_messages; i++)
            free (msg[i]);
        free (msg);
        if (result != PAM_SUCCESS)
            return result;

        if (resp == NULL)
            return PAM_CONV_ERR;
        if (resp[password_index].resp == NULL)
        {
            free (resp);
            return PAM_CONV_ERR;
        }

        if (entry)
            password_matches = strcmp (entry->pw_passwd, resp[password_index].resp) == 0;

        if (password_matches && strcmp (pamh->user, "multi-prompt") == 0)
            password_matches = strcmp ("blue", resp[0].resp) == 0;

        for (int i = 0; i < n_messages; i++)
        {
            if (resp[i].resp)
                free (resp[i].resp);
        }
        free (resp);

        /* Do two factor authentication */
        if (password_matches && strcmp (pamh->user, "two-factor") == 0)
        {
            msg = malloc (sizeof (struct pam_message *) * 1);
            msg[0] = malloc (sizeof (struct pam_message));
            msg[0]->msg_style = PAM_PROMPT_ECHO_ON;
            msg[0]->msg = "OTP:";
            resp = NULL;
            result = pamh->conversation.conv (1, (const struct pam_message **) msg, &resp, pamh->conversation.appdata_ptr);
            free (msg[0]);
            free (msg);
            if (result != PAM_SUCCESS)
                return result;

            if (resp == NULL)
                return PAM_CONV_ERR;
            if (resp[0].resp == NULL)
            {
                free (resp);
                return PAM_CONV_ERR;
            }
            password_matches = strcmp (resp[0].resp, "otp") == 0;
            free (resp[0].resp);
            free (resp);
        }
    }

    /* Special user has home directory created on login */
    if (password_matches && strcmp (pamh->user, "mount-home-dir") == 0)
        g_mkdir_with_parents (entry->pw_dir, 0755);

    /* Special user 'change-user1' changes user on authentication */
    if (password_matches && strcmp (pamh->user, "change-user1") == 0)
    {
        g_free (pamh->user);
        pamh->user = g_strdup ("change-user2");
    }

    /* Special user 'change-user-invalid' changes to an invalid user on authentication */
    if (password_matches && strcmp (pamh->user, "change-user-invalid") == 0)
    {
        g_free (pamh->user);
        pamh->user = g_strdup ("invalid-user");
    }

    if (password_matches)
        return PAM_SUCCESS;
    else
        return PAM_AUTH_ERR;
}

static const char *
get_env_value (const char *name_value, const char *name)
{
    int j;
    for (j = 0; name[j] && name_value[j] && name[j] == name_value[j]; j++);
    if (name[j] == '\0' && name_value[j] == '=')
        return &name_value[j + 1];

    return NULL;
}

int
pam_putenv (pam_handle_t *pamh, const char *name_value)
{
    g_autofree char *name = strdup (name_value);
    for (int i = 0; name[i]; i++)
        if (name[i] == '=')
            name[i] = '\0';
    int i;
    for (i = 0; pamh->envlist[i]; i++)
    {
        if (get_env_value (pamh->envlist[i], name))
            break;
    }

    if (pamh->envlist[i])
    {
        free (pamh->envlist[i]);
        pamh->envlist[i] = strdup (name_value);
    }
    else
    {
        pamh->envlist = realloc (pamh->envlist, sizeof (char *) * (i + 2));
        pamh->envlist[i] = strdup (name_value);
        pamh->envlist[i + 1] = NULL;
    }

    return PAM_SUCCESS;
}

const char *
pam_getenv (pam_handle_t *pamh, const char *name)
{
    for (int i = 0; pamh->envlist[i]; i++)
    {
        const char *value = get_env_value (pamh->envlist[i], name);
        if (value)
            return value;
    }

    return NULL;
}

char **
pam_getenvlist (pam_handle_t *pamh)
{
    return pamh->envlist;
}

int
pam_set_item (pam_handle_t *pamh, int item_type, const void *item)
{
    if (item == NULL)
        return PAM_SYSTEM_ERR;

    switch (item_type)
    {
    case PAM_TTY:
        if (pamh->tty)
            free (pamh->tty);
        pamh->tty = strdup ((const char *) item);
        return PAM_SUCCESS;

    default:
        return PAM_BAD_ITEM;
    }
}

int
pam_get_item (const pam_handle_t *pamh, int item_type, const void **item)
{
    if (item == NULL)
        return PAM_SYSTEM_ERR;

    switch (item_type)
    {
    case PAM_SERVICE:
        *item = pamh->service_name;
        return PAM_SUCCESS;

    case PAM_USER:
        *item = pamh->user;
        return PAM_SUCCESS;

    case PAM_AUTHTOK:
        *item = pamh->authtok;
        return PAM_SUCCESS;

    case PAM_RUSER:
        *item = pamh->ruser;
        return PAM_SUCCESS;

    case PAM_USER_PROMPT:
        *item = LOGIN_PROMPT;
        return PAM_SUCCESS;

    case PAM_TTY:
        *item = pamh->tty;
        return PAM_SUCCESS;

    case PAM_CONV:
        *item = &pamh->conversation;
        return PAM_SUCCESS;

    default:
        return PAM_BAD_ITEM;
    }
}

int
pam_open_session (pam_handle_t *pamh, int flags)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-pam", "log-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("");
        g_string_append_printf (status, "%s OPEN-SESSION", pamh->id);
        if (flags & PAM_SILENT)
            g_string_append (status, " SILENT");

        status_notify ("%s", status->str);
    }

    if (strcmp (pamh->user, "session-error") == 0)
        return PAM_SESSION_ERR;

    if (strcmp (pamh->user, "make-home-dir") == 0)
    {
        struct passwd *entry = getpwnam (pamh->user);
        g_mkdir_with_parents (entry->pw_dir, 0755);
    }

    /* Open logind session */
    g_autoptr(GError) error = NULL;
    g_autoptr(GVariant) result = g_dbus_connection_call_sync (g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL),
                                                              "org.freedesktop.login1",
                                                              "/org/freedesktop/login1",
                                                              "org.freedesktop.login1.Manager",
                                                              "CreateSession",
                                                              g_variant_new ("()", ""),
                                                              G_VARIANT_TYPE ("(so)"),
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              G_MAXINT,
                                                              NULL,
                                                              &error);
    if (result)
    {
        const gchar *id;
        g_variant_get (result, "(&so)", &id, NULL);
        g_autofree gchar *e = g_strdup_printf ("XDG_SESSION_ID=%s", id);
        pam_putenv (pamh, e);
    }
    else
        g_printerr ("Failed to create logind session: %s\n", error->message);

    return PAM_SUCCESS;
}

int
pam_close_session (pam_handle_t *pamh, int flags)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-pam", "log-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("");
        g_string_append_printf (status, "%s CLOSE-SESSION", pamh->id);
        if (flags & PAM_SILENT)
            g_string_append (status, " SILENT");

        status_notify ("%s", status->str);
    }

    return PAM_SUCCESS;
}

int
pam_acct_mgmt (pam_handle_t *pamh, int flags)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-pam", "log-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("");
        g_string_append_printf (status, "%s ACCT-MGMT", pamh->id);
        if (flags & PAM_SILENT)
            g_string_append (status, " SILENT");
        if (flags & PAM_DISALLOW_NULL_AUTHTOK)
            g_string_append (status, " DISALLOW_NULL_AUTHTOK");

        status_notify ("%s", status->str);
    }

    if (!pamh->user)
        return PAM_USER_UNKNOWN;

    if (strcmp (pamh->user, "denied") == 0)
        return PAM_PERM_DENIED;
    if (strcmp (pamh->user, "expired") == 0)
        return PAM_ACCT_EXPIRED;
    if (strcmp (pamh->user, "new-authtok") == 0)
        return PAM_NEW_AUTHTOK_REQD;

    return PAM_SUCCESS;
}

int
pam_chauthtok (pam_handle_t *pamh, int flags)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-pam", "log-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("");
        g_string_append_printf (status, "%s CHAUTHTOK", pamh->id);
        if (flags & PAM_SILENT)
            g_string_append (status, " SILENT");
        if (flags & PAM_CHANGE_EXPIRED_AUTHTOK)
            g_string_append (status, " CHANGE_EXPIRED_AUTHTOK");

        status_notify ("%s", status->str);
    }

    struct pam_message **msg = malloc (sizeof (struct pam_message *) * 1);
    msg[0] = malloc (sizeof (struct pam_message));
    msg[0]->msg_style = PAM_PROMPT_ECHO_OFF;
    if ((flags & PAM_CHANGE_EXPIRED_AUTHTOK) != 0)
        msg[0]->msg = "Enter new password (expired):";
    else
        msg[0]->msg = "Enter new password:";
    struct pam_response *resp = NULL;
    int result = pamh->conversation.conv (1, (const struct pam_message **) msg, &resp, pamh->conversation.appdata_ptr);
    free (msg[0]);
    free (msg);
    if (result != PAM_SUCCESS)
        return result;

    if (resp == NULL)
        return PAM_CONV_ERR;
    if (resp[0].resp == NULL)
    {
        free (resp);
        return PAM_CONV_ERR;
    }

    /* Update password database */
    struct passwd *entry = getpwnam (pamh->user);
    free (entry->pw_passwd);
    entry->pw_passwd = resp[0].resp;
    free (resp);

    return PAM_SUCCESS;
}

int
pam_setcred (pam_handle_t *pamh, int flags)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-pam", "log-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("");
        g_string_append_printf (status, "%s SETCRED", pamh->id);
        if (flags & PAM_SILENT)
            g_string_append (status, " SILENT");
        if (flags & PAM_ESTABLISH_CRED)
            g_string_append (status, " ESTABLISH_CRED");
        if (flags & PAM_DELETE_CRED)
            g_string_append (status, " DELETE_CRED");
        if (flags & PAM_REINITIALIZE_CRED)
            g_string_append (status, " REINITIALIZE_CRED");
        if (flags & PAM_REFRESH_CRED)
            g_string_append (status, " REFRESH_CRED");

        status_notify ("%s", status->str);
    }

    /* Put the test directories into the path */
    g_autofree gchar *e = g_strdup_printf ("PATH=%s/tests/src/.libs:%s/tests/src:%s/tests/src:%s/src:%s", BUILDDIR, BUILDDIR, SRCDIR, BUILDDIR, pam_getenv (pamh, "PATH"));
    pam_putenv (pamh, e);

    if (strcmp (pamh->user, "cred-error") == 0)
        return PAM_CRED_ERR;
    if (strcmp (pamh->user, "cred-expired") == 0)
        return PAM_CRED_EXPIRED;
    if (strcmp (pamh->user, "cred-unavail") == 0)
        return PAM_CRED_UNAVAIL;

    /* Join special groups if requested */
    if (strcmp (pamh->user, "group-member") == 0 && flags & PAM_ESTABLISH_CRED)
    {
        struct group *group = getgrnam ("test-group");
        if (group)
        {
            int groups_length = getgroups (0, NULL);
            if (groups_length < 0)
                return PAM_SYSTEM_ERR;
            gid_t *groups = malloc (sizeof (gid_t) * (groups_length + 1));
            groups_length = getgroups (groups_length, groups);
            if (groups_length < 0)
                return PAM_SYSTEM_ERR;
            groups[groups_length] = group->gr_gid;
            groups_length++;
            setgroups (groups_length, groups);
            free (groups);
        }

        /* We need to pass our group overrides down the child process - the environment via PAM seems the only way to do it easily */
        pam_putenv (pamh, g_strdup_printf ("LIGHTDM_TEST_GROUPS=%s", g_getenv ("LIGHTDM_TEST_GROUPS")));
    }

    return PAM_SUCCESS;
}

int
pam_end (pam_handle_t *pamh, int pam_status)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-pam", "log-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("");
        g_string_append_printf (status, "%s END", pamh->id);
        status_notify ("%s", status->str);
    }

    free (pamh->id);
    free (pamh->service_name);
    if (pamh->user)
        free (pamh->user);
    if (pamh->authtok)
        free (pamh->authtok);
    if (pamh->ruser)
        free (pamh->ruser);
    if (pamh->tty)
        free (pamh->tty);
    free (pamh);

    return PAM_SUCCESS;
}

const char *
pam_strerror (pam_handle_t *pamh, int errnum)
{
    if (pamh == NULL)
        return NULL;

    switch (errnum)
    {
    case PAM_SUCCESS:
        return "Success";
    case PAM_ABORT:
        return "Critical error - immediate abort";
    case PAM_OPEN_ERR:
        return "Failed to load module";
    case PAM_SYMBOL_ERR:
        return "Symbol not found";
    case PAM_SERVICE_ERR:
        return "Error in service module";
    case PAM_SYSTEM_ERR:
        return "System error";
    case PAM_BUF_ERR:
        return "Memory buffer error";
    case PAM_PERM_DENIED:
        return "Permission denied";
    case PAM_AUTH_ERR:
        return "Authentication failure";
    case PAM_CRED_INSUFFICIENT:
        return "Insufficient credentials to access authentication data";
    case PAM_AUTHINFO_UNAVAIL:
        return "Authentication service cannot retrieve authentication info";
    case PAM_USER_UNKNOWN:
        return "User not known to the underlying authentication module";
    case PAM_MAXTRIES:
        return "Have exhausted maximum number of retries for service";
    case PAM_NEW_AUTHTOK_REQD:
        return "Authentication token is no longer valid; new one required";
    case PAM_ACCT_EXPIRED:
        return "User account has expired";
    case PAM_SESSION_ERR:
        return "Cannot make/remove an entry for the specified session";
    case PAM_CRED_UNAVAIL:
        return "Authentication service cannot retrieve user credentials";
    case PAM_CRED_EXPIRED:
        return "User credentials expired";
    case PAM_CRED_ERR:
        return "Failure setting user credentials";
    case PAM_NO_MODULE_DATA:
        return "No module specific data is present";
    case PAM_BAD_ITEM:
        return "Bad item passed to pam_*_item()";
    case PAM_CONV_ERR:
        return "Conversation error";
    case PAM_AUTHTOK_ERR:
        return "Authentication token manipulation error";
    case PAM_AUTHTOK_RECOVERY_ERR:
        return "Authentication information cannot be recovered";
    case PAM_AUTHTOK_LOCK_BUSY:
        return "Authentication token lock busy";
    case PAM_AUTHTOK_DISABLE_AGING:
        return "Authentication token aging disabled";
    case PAM_TRY_AGAIN:
        return "Failed preliminary check by password service";
    case PAM_IGNORE:
        return "The return value should be ignored by PAM dispatch";
    case PAM_MODULE_UNKNOWN:
        return "Module is unknown";
    case PAM_AUTHTOK_EXPIRED:
        return "Authentication token expired";
    case PAM_CONV_AGAIN:
        return "Conversation is waiting for event";
    case PAM_INCOMPLETE:
        return "Application needs to call libpam again";
    default:
        return "Unknown PAM error";
    }
}

void
setutxent (void)
{
}

struct utmpx *
pututxline (const struct utmpx *ut)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-utmp-config", "check-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("UTMP");
        switch (ut->ut_type)
        {
        case INIT_PROCESS:
            g_string_append_printf (status, " TYPE=INIT_PROCESS");
            break;
        case LOGIN_PROCESS:
            g_string_append_printf (status, " TYPE=LOGIN_PROCESS");
            break;
        case USER_PROCESS:
            g_string_append_printf (status, " TYPE=USER_PROCESS");
            break;
        case DEAD_PROCESS:
            g_string_append_printf (status, " TYPE=DEAD_PROCESS");
            break;
        default:
            g_string_append_printf (status, " TYPE=%d", ut->ut_type);
        }
        if (ut->ut_line)
            g_string_append_printf (status, " LINE=%s", ut->ut_line);
        if (ut->ut_id)
            g_string_append_printf (status, " ID=%s", ut->ut_id);
        if (ut->ut_user)
            g_string_append_printf (status, " USER=%s", ut->ut_user);
        if (ut->ut_host)
            g_string_append_printf (status, " HOST=%s", ut->ut_host);
        status_notify ("%s", status->str);
    }

    return (struct utmpx *)ut;
}

void
endutxent (void)
{
}

void
updwtmp (const char *wtmp_file, const struct utmp *ut)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-utmp-config", "check-events", NULL))
    {
        g_autoptr(GString) status = g_string_new ("WTMP");
        g_string_append_printf (status, " FILE=%s", wtmp_file);
        switch (ut->ut_type)
        {
        case INIT_PROCESS:
            g_string_append_printf (status, " TYPE=INIT_PROCESS");
            break;
        case LOGIN_PROCESS:
            g_string_append_printf (status, " TYPE=LOGIN_PROCESS");
            break;
        case USER_PROCESS:
            g_string_append_printf (status, " TYPE=USER_PROCESS");
            break;
        case DEAD_PROCESS:
            g_string_append_printf (status, " TYPE=DEAD_PROCESS");
            break;
        default:
            g_string_append_printf (status, " TYPE=%d", ut->ut_type);
        }
        if (ut->ut_line)
            g_string_append_printf (status, " LINE=%s", ut->ut_line);
        if (ut->ut_id)
            g_string_append_printf (status, " ID=%s", ut->ut_id);
        if (ut->ut_user)
            g_string_append_printf (status, " USER=%s", ut->ut_user);
        if (ut->ut_host)
            g_string_append_printf (status, " HOST=%s", ut->ut_host);
        status_notify ("%s", status->str);
    }
}

struct xcb_connection_t
{
    gchar *display;
    int error;
    GSocket *socket;
};

xcb_connection_t *
xcb_connect_to_display_with_auth_info (const char *display, xcb_auth_info_t *auth, int *screen)
{
    xcb_connection_t *c = malloc (sizeof (xcb_connection_t));
    c->display = g_strdup (display);
    c->error = 0;

    if (display == NULL)
        display = getenv ("DISPLAY");
    if (display == NULL)
        c->error = XCB_CONN_CLOSED_PARSE_ERR;

    if (c->error == 0)
    {
        g_autoptr(GError) error = NULL;
        c->socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
        if (c->socket == NULL)
        {
            g_printerr ("%s\n", error->message);
            c->error = XCB_CONN_ERROR;
        }
    }

    if (c->error == 0)
    {
        /* Skip the hostname, we'll assume it's localhost */
        g_autofree gchar *d = g_strdup_printf (".x%s", strchr (display, ':'));
        g_autofree gchar *socket_path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), d, NULL);
        g_autoptr(GSocketAddress) address = g_unix_socket_address_new (socket_path);
        g_autoptr(GError) error = NULL;
        if (!g_socket_connect (c->socket, address, NULL, &error))
        {
            g_printerr ("Failed to connect to X socket %s: %s\n", socket_path, error->message);
            c->error = XCB_CONN_ERROR;
        }
    }

    // FIXME: Send auth info
    if (c->error == 0)
    {
    }

    return c;
}

xcb_connection_t *
xcb_connect (const char *displayname, int *screenp)
{
    return xcb_connect_to_display_with_auth_info(displayname, NULL, screenp);
}

int
xcb_connection_has_error (xcb_connection_t *c)
{
    return c->error;
}

void
xcb_disconnect (xcb_connection_t *c)
{
    free (c->display);
    if (c->socket)
    {
        g_socket_close (c->socket, NULL);
        g_object_unref (c->socket);
    }
    free (c);
}

#if HAVE_LIBAUDIT
int
audit_open (void)
{
    connect_status ();
    if (g_key_file_get_boolean (config, "test-audit-config", "check-events", NULL))
        status_notify ("AUDIT OPEN");

    return dup (STDOUT_FILENO);
}

int
audit_log_acct_message (int audit_fd, int type, const char *pgname,
                        const char *op, const char *name, unsigned int id,
                        const char *host, const char *addr, const char *tty, int result)
{
    connect_status ();
    if (!g_key_file_get_boolean (config, "test-audit-config", "check-events", NULL))
        return 1;

    g_autofree gchar *type_string = NULL;
    switch (type)
    {
    case AUDIT_USER_LOGIN:
        type_string = g_strdup ("USER_LOGIN");
        break;
    case AUDIT_USER_LOGOUT:
        type_string = g_strdup ("USER_LOGOUT");
        break;
    default:
        type_string = g_strdup_printf ("%d", type);
        break;
    }

    status_notify ("AUDIT LOG-ACCT TYPE=%s PGNAME=%s OP=%s NAME=%s ID=%u HOST=%s ADDR=%s TTY=%s RESULT=%d",
                   type_string, pgname ? pgname : "", op ? op : "", name ? name : "", id, host ? host : "", addr ? addr : "", tty ? tty : "", result);

    return 1;
}

#endif
