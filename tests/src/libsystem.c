#define _GNU_SOURCE
#define __USE_GNU

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <pwd.h>
#include <unistd.h>
#include <dirent.h>
#include <grp.h>
#include <security/pam_appl.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <utmpx.h>
#ifdef __linux__
#include <linux/vt.h>
#endif
#include <glib.h>
#include <xcb/xcb.h>
#include <gio/gunixsocketaddress.h>

#include "status.h"

#define LOGIN_PROMPT "login:"

static int console_fd = -1;

static GList *user_entries = NULL;
static GList *getpwent_link = NULL;

static GList *group_entries = NULL;

static int active_vt = 7;

static gboolean status_connected = FALSE;

struct pam_handle
{
    char *service_name;
    char *user;
    char *authtok;
    char *ruser;
    char *tty;
    char **envlist;
    struct pam_conv conversation;
};

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
    const gchar *group_list;
    gchar **groups;
    gint groups_length;

    /* Get groups we are a member of */
    group_list = g_getenv ("LIGHTDM_TEST_GROUPS");
    if (!group_list)
        group_list = "";
    groups = g_strsplit (group_list, ",", -1);
    groups_length = g_strv_length (groups);

    if (size != 0)
    {
        int i;

        if (groups_length > size)
        {
            errno = EINVAL;
            return -1;
        }
        for (i = 0; groups[i]; i++)
            list[i] = atoi (groups[i]);
    }
    g_free (groups);

    return groups_length;
}

int
setgroups (size_t size, const gid_t *list)
{
    size_t i;
    GString *group_list;

    group_list = g_string_new ("");
    for (i = 0; i < size; i++)
    {
        if (i != 0)
            g_string_append (group_list, ",");
        g_string_append_printf (group_list, "%d", list[i]);
    }
    g_setenv ("LIGHTDM_TEST_GROUPS", group_list->str, TRUE);
    g_string_free (group_list, TRUE);

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

    return g_strdup (path);
}

#ifdef __linux__
static int
open_wrapper (const char *func, const char *pathname, int flags, mode_t mode)
{
    int (*_open) (const char *pathname, int flags, mode_t mode);
    gchar *new_path = NULL;
    int fd;

    _open = (int (*)(const char *pathname, int flags, mode_t mode)) dlsym (RTLD_NEXT, func);

    if (strcmp (pathname, "/dev/console") == 0)
    {
        if (console_fd < 0)
        {
            console_fd = _open ("/dev/null", flags, mode);
            fcntl (console_fd, F_SETFD, FD_CLOEXEC);
        }
        return console_fd;
    }

    new_path = redirect_path (pathname);
    fd = _open (new_path, flags, mode);
    g_free (new_path);

    return fd;
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
    FILE *(*_fopen) (const char *pathname, const char *mode);
    gchar *new_path = NULL;
    FILE *result;

    _fopen = (FILE *(*)(const char *pathname, const char *mode)) dlsym (RTLD_NEXT, "fopen");

    new_path = redirect_path (path);
    result = _fopen (new_path, mode);
    g_free (new_path);

    return result;
}

int
unlinkat (int dirfd, const char *pathname, int flags)
{
    int (*_unlinkat) (int dirfd, const char *pathname, int flags);
    gchar *new_path = NULL;
    int result;

    _unlinkat = (int (*)(int dirfd, const char *pathname, int flags)) dlsym (RTLD_NEXT, "unlinkat");

    new_path = redirect_path (pathname);
    result = _unlinkat (dirfd, new_path, flags);
    g_free (new_path);

    return result;
}

int
creat (const char *pathname, mode_t mode)
{
    int (*_creat) (const char *pathname, mode_t mode);
    gchar *new_path = NULL;
    int result;

    _creat = (int (*)(const char *pathname, mode_t mode)) dlsym (RTLD_NEXT, "creat");

    new_path = redirect_path (pathname);
    result = _creat (new_path, mode);
    g_free (new_path);

    return result;
}

int
creat64 (const char *pathname, mode_t mode)
{
    int (*_creat64) (const char *pathname, mode_t mode);
    gchar *new_path = NULL;
    int result;

    _creat64 = (int (*)(const char *pathname, mode_t mode)) dlsym (RTLD_NEXT, "creat64");

    new_path = redirect_path (pathname);
    result = _creat64 (new_path, mode);
    g_free (new_path);

    return result;
}

int
access (const char *pathname, int mode)
{
    int (*_access) (const char *pathname, int mode);
    gchar *new_path = NULL;
    int ret;

    /* Look like systemd is always running */
    if (strcmp (pathname, "/run/systemd/seats/") == 0)
        return 1;

    _access = (int (*)(const char *pathname, int mode)) dlsym (RTLD_NEXT, "access");

    new_path = redirect_path (pathname);
    ret = _access (new_path, mode);
    g_free (new_path);

    return ret;
}

int
stat (const char *path, struct stat *buf)
{
    int (*_stat) (const char *path, struct stat *buf);
    gchar *new_path = NULL;
    int ret;
  
    _stat = (int (*)(const char *path, struct stat *buf)) dlsym (RTLD_NEXT, "stat");

    new_path = redirect_path (path);
    ret = _stat (new_path, buf);
    g_free (new_path);

    return ret;
}

int
stat64 (const char *path, struct stat64 *buf)
{
    int (*_stat64) (const char *path, struct stat64 *buf);
    gchar *new_path = NULL;
    int ret;

    _stat64 = (int (*)(const char *path, struct stat64 *buf)) dlsym (RTLD_NEXT, "stat64");

    new_path = redirect_path (path);
    ret = _stat64 (new_path, buf);
    g_free (new_path);

    return ret;
}

int
__xstat (int version, const char *path, struct stat *buf)
{
    int (*___xstat) (int version, const char *path, struct stat *buf);
    gchar *new_path = NULL;
    int ret;
  
    ___xstat = (int (*)(int version, const char *path, struct stat *buf)) dlsym (RTLD_NEXT, "__xstat");

    new_path = redirect_path (path);
    ret = ___xstat (version, new_path, buf);
    g_free (new_path);

    return ret;
}

int
__xstat64 (int version, const char *path, struct stat64 *buf)
{
    int (*___xstat64) (int version, const char *path, struct stat64 *buf);
    gchar *new_path = NULL;
    int ret;
  
    ___xstat64 = (int (*)(int version, const char *path, struct stat64 *buf)) dlsym (RTLD_NEXT, "__xstat64");

    new_path = redirect_path (path);
    ret = ___xstat64 (version, new_path, buf);
    g_free (new_path);

    return ret;
}

int
__fxstatat(int ver, int dirfd, const char *pathname, struct stat *buf, int flags)
{
    int (*___fxstatat) (int ver, int dirfd, const char *pathname, struct stat *buf, int flags);
    gchar *new_path = NULL;
    int ret;
  
    ___fxstatat = (int (*)(int ver, int dirfd, const char *pathname, struct stat *buf, int flags)) dlsym (RTLD_NEXT, "__fxstatat");

    new_path = redirect_path (pathname);
    ret = ___fxstatat (ver, dirfd, new_path, buf, flags);
    g_free (new_path);

    return ret;
}

int
__fxstatat64(int ver, int dirfd, const char *pathname, struct stat64 *buf, int flags)
{
    int (*___fxstatat64) (int ver, int dirfd, const char *pathname, struct stat64 *buf, int flags);
    gchar *new_path = NULL;
    int ret;
  
    ___fxstatat64 = (int (*)(int ver, int dirfd, const char *pathname, struct stat64 *buf, int flags)) dlsym (RTLD_NEXT, "__fxstatat64");

    new_path = redirect_path (pathname);
    ret = ___fxstatat64 (ver, dirfd, new_path, buf, flags);
    g_free (new_path);

    return ret;
}

DIR *
opendir (const char *name)
{
    DIR *(*_opendir) (const char *name);
    gchar *new_path = NULL;
    DIR *result;

    _opendir = (DIR *(*)(const char *name)) dlsym (RTLD_NEXT, "opendir");

    new_path = redirect_path (name);
    result = _opendir (new_path);
    g_free (new_path);

    return result; 
}

int
mkdir (const char *pathname, mode_t mode)
{
    int (*_mkdir) (const char *pathname, mode_t mode);
    gchar *new_path = NULL;
    int result;

    _mkdir = (int (*)(const char *pathname, mode_t mode)) dlsym (RTLD_NEXT, "mkdir");

    new_path = redirect_path (pathname);
    result = _mkdir (new_path, mode);
    g_free (new_path);

    return result;
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
    int (*_chmod) (const char *path, mode_t mode);
    gchar *new_path = NULL;
    int result;

    _chmod = (int (*)(const char *path, mode_t mode)) dlsym (RTLD_NEXT, "chmod");

    new_path = redirect_path (path);
    result = _chmod (new_path, mode);
    g_free (new_path);

    return result;
}

int
ioctl (int d, unsigned long request, ...)
{
    int (*_ioctl) (int d, int request, ...);

    _ioctl = (int (*)(int d, int request, ...)) dlsym (RTLD_NEXT, "ioctl");
    if (d > 0 && d == console_fd)
    {
        struct vt_stat *console_state;
        int vt;
        va_list ap;

        switch (request)
        {
        case VT_GETSTATE:
            va_start (ap, request);
            console_state = va_arg (ap, struct vt_stat *);
            va_end (ap);
            console_state->v_active = active_vt;
            break;
        case VT_ACTIVATE:
            va_start (ap, request);
            vt = va_arg (ap, int);
            va_end (ap);
            if (vt != active_vt)
            {
                active_vt = vt;
                if (!status_connected)
                    status_connected = status_connect (NULL, NULL);
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
        void *data;

        va_start (ap, request);
        data = va_arg (ap, void *);
        va_end (ap);
        return _ioctl (d, request, data);
    }
}

int
close (int fd)
{
    int (*_close) (int fd);

    if (fd > 0 && fd == console_fd)
        return 0;

    _close = (int (*)(int fd)) dlsym (RTLD_NEXT, "close");
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
    gchar *path, *data = NULL, **lines;
    gint i;
    GError *error = NULL;

    g_list_free_full (user_entries, free_user);
    user_entries = NULL;
    getpwent_link = NULL;

    path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "etc", "passwd", NULL);
    g_file_get_contents (path, &data, NULL, &error);
    g_free (path);
    if (error)
        g_warning ("Error loading passwd file: %s", error->message);
    g_clear_error (&error);

    if (!data)
        return;

    lines = g_strsplit (data, "\n", -1);
    g_free (data);

    for (i = 0; lines[i]; i++)
    {
        gchar *line, **fields;

        line = g_strstrip (lines[i]);
        fields = g_strsplit (line, ":", -1);
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
        g_strfreev (fields);
    }
    g_strfreev (lines);
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
    GList *link;
  
    if (name == NULL)
        return NULL;
  
    load_passwd_file ();

    for (link = user_entries; link; link = link->next)
    {
        struct passwd *entry = link->data;
        if (strcmp (entry->pw_name, name) == 0)
            break;
    }
    if (!link)
        return NULL;

    return link->data;
}

struct passwd *
getpwuid (uid_t uid)
{
    GList *link;

    load_passwd_file ();

    for (link = user_entries; link; link = link->next)
    {
        struct passwd *entry = link->data;
        if (entry->pw_uid == uid)
            break;
    }
    if (!link)
        return NULL;

    return link->data;
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
    gchar *path, *data = NULL, **lines;
    gint i;
    GError *error = NULL;

    g_list_free_full (group_entries, free_group);
    group_entries = NULL;

    path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), "etc", "group", NULL);
    g_file_get_contents (path, &data, NULL, &error);
    g_free (path);
    if (error)
        g_warning ("Error loading group file: %s", error->message);
    g_clear_error (&error);

    if (!data)
        return;

    lines = g_strsplit (data, "\n", -1);
    g_free (data);

    for (i = 0; lines[i]; i++)
    {
        gchar *line, **fields;

        line = g_strstrip (lines[i]);
        fields = g_strsplit (line, ":", -1);
        if (g_strv_length (fields) == 4)
        {
            struct group *entry = malloc (sizeof (struct group));

            entry->gr_name = g_strdup (fields[0]);
            entry->gr_passwd = g_strdup (fields[1]);
            entry->gr_gid = atoi (fields[2]);
            entry->gr_mem = g_strsplit (fields[3], ",", -1);
            group_entries = g_list_append (group_entries, entry);
        }
        g_strfreev (fields);
    }
    g_strfreev (lines);
}

struct group *
getgrnam (const char *name)
{
    GList *link;

    load_group_file ();

    for (link = group_entries; link; link = link->next)
    {
        struct group *entry = link->data;
        if (strcmp (entry->gr_name, name) == 0)
            break;
    }
    if (!link)
        return NULL;

    return link->data;
}

struct group *
getgrgid (gid_t gid)
{
    GList *link;

    load_group_file ();

    for (link = group_entries; link; link = link->next)
    {
        struct group *entry = link->data;
        if (entry->gr_gid == gid)
            break;
    }
    if (!link)
        return NULL;

    return link->data;
}

int
pam_start (const char *service_name, const char *user, const struct pam_conv *conversation, pam_handle_t **pamh)
{
    pam_handle_t *handle;

    if (service_name == NULL || conversation == NULL || pamh == NULL)
        return PAM_SYSTEM_ERR;

    handle = *pamh = malloc (sizeof (pam_handle_t));
    if (handle == NULL)
        return PAM_BUF_ERR;

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

static void
send_info (pam_handle_t *pamh, const char *message)
{
    struct pam_message **msg;
    struct pam_response *resp = NULL;

    msg = calloc (1, sizeof (struct pam_message *));
    msg[0] = malloc (sizeof (struct pam_message));
    msg[0]->msg_style = PAM_TEXT_INFO;
    msg[0]->msg = message;
    pamh->conversation.conv (1, (const struct pam_message **) msg, &resp, pamh->conversation.appdata_ptr);
    free (msg[0]);
    free (msg);
    if (resp)
    {
        if (resp[0].resp)
            free (resp[0].resp);
        free (resp);
    }
}

int
pam_authenticate (pam_handle_t *pamh, int flags)
{
    struct passwd *entry;
    gboolean password_matches = FALSE;

    if (pamh == NULL)
        return PAM_SYSTEM_ERR;
  
    if (strcmp (pamh->service_name, "test-remote") == 0)
    {
        int result;
        struct pam_message **msg;
        struct pam_response *resp = NULL;

        msg = malloc (sizeof (struct pam_message *) * 1);
        msg[0] = malloc (sizeof (struct pam_message));
        msg[0]->msg_style = PAM_PROMPT_ECHO_ON; 
        msg[0]->msg = "remote-login:";
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
        int result;
        struct pam_message **msg;
        struct pam_response *resp = NULL;

        msg = malloc (sizeof (struct pam_message *) * 1);
        msg[0] = malloc (sizeof (struct pam_message));
        msg[0]->msg_style = PAM_PROMPT_ECHO_ON; 
        msg[0]->msg = LOGIN_PROMPT;
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
      
        pamh->user = strdup (resp[0].resp);
        free (resp[0].resp);
        free (resp);
    }

    if (strcmp (pamh->user, "log-pam") == 0)
        send_info (pamh, "pam_authenticate");

    /* Crash on authenticate */
    if (strcmp (pamh->user, "crash-authenticate") == 0)
        kill (getpid (), SIGSEGV);

    /* Look up password database */
    entry = getpwnam (pamh->user);

    /* Prompt for password if required */
    if (entry && strcmp (pamh->user, "always-password") != 0 && (strcmp (pamh->service_name, "lightdm-autologin") == 0 || strcmp (entry->pw_passwd, "") == 0))
        password_matches = TRUE;
    else
    {
        int i, n_messages = 0, password_index, result;
        struct pam_message **msg;
        struct pam_response *resp = NULL;

        msg = malloc (sizeof (struct pam_message *) * 5);
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
        password_index = n_messages;
        n_messages++;
        result = pamh->conversation.conv (n_messages, (const struct pam_message **) msg, &resp, pamh->conversation.appdata_ptr);
        for (i = 0; i < n_messages; i++)
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

        for (i = 0; i < n_messages; i++)
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
    int i;
    gchar *name;

    if (pamh == NULL || name_value == NULL)
        return PAM_SYSTEM_ERR;

    name = strdup (name_value);
    for (i = 0; name[i]; i++)
        if (name[i] == '=')
            name[i] = '\0';
    for (i = 0; pamh->envlist[i]; i++)
    {
        if (get_env_value (pamh->envlist[i], name))
            break;
    }
    free (name);

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
    int i;

    if (pamh == NULL || name == NULL)
        return NULL;

    for (i = 0; pamh->envlist[i]; i++)
    {
        const char *value;
        value = get_env_value (pamh->envlist[i], name);
        if (value)
            return value;
    }

    return NULL;
}

char **
pam_getenvlist (pam_handle_t *pamh)
{
    if (pamh == NULL)
        return NULL;

    return pamh->envlist;
}

int
pam_set_item (pam_handle_t *pamh, int item_type, const void *item)
{
    if (pamh == NULL || item == NULL)
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
    if (pamh == NULL || item == NULL)
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
    if (pamh == NULL)
        return PAM_SYSTEM_ERR;

    if (strcmp (pamh->user, "session-error") == 0)
        return PAM_SESSION_ERR;

    if (strcmp (pamh->user, "log-pam") == 0)
        send_info (pamh, "pam_open_session");

    if (strcmp (pamh->user, "make-home-dir") == 0)
    {
        struct passwd *entry;
        entry = getpwnam (pamh->user);
        g_mkdir_with_parents (entry->pw_dir, 0755);
    }

    return PAM_SUCCESS;
}

int
pam_close_session (pam_handle_t *pamh, int flags)
{
    if (pamh == NULL)
        return PAM_SYSTEM_ERR;

    if (strcmp (pamh->user, "log-pam") == 0)
        send_info (pamh, "pam_close_session");

    return PAM_SUCCESS;
}

int
pam_acct_mgmt (pam_handle_t *pamh, int flags)
{
    if (pamh == NULL)
        return PAM_SYSTEM_ERR;
  
    if (!pamh->user)
        return PAM_USER_UNKNOWN;

    if (strcmp (pamh->user, "log-pam") == 0)
        send_info (pamh, "pam_acct_mgmt");

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
    struct passwd *entry;
    int result;
    struct pam_message **msg;
    struct pam_response *resp = NULL;

    if (pamh == NULL)
        return PAM_SYSTEM_ERR;

    if (strcmp (pamh->user, "log-pam") == 0)
        send_info (pamh, "pam_chauthtok");

    msg = malloc (sizeof (struct pam_message *) * 1);
    msg[0] = malloc (sizeof (struct pam_message));
    msg[0]->msg_style = PAM_PROMPT_ECHO_OFF;
    if ((flags & PAM_CHANGE_EXPIRED_AUTHTOK) != 0)
        msg[0]->msg = "Enter new password (expired):";
    else
        msg[0]->msg = "Enter new password:";
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

    /* Update password database */
    entry = getpwnam (pamh->user);
    free (entry->pw_passwd);
    entry->pw_passwd = resp[0].resp;
    free (resp);

    return PAM_SUCCESS;
}

int
pam_setcred (pam_handle_t *pamh, int flags)
{
    gchar *e;

    if (pamh == NULL)
        return PAM_SYSTEM_ERR;

    if (strcmp (pamh->user, "log-pam") == 0)
        send_info (pamh, "pam_setcred");

    /* Put the test directories into the path */
    e = g_strdup_printf ("PATH=%s/tests/src/.libs:%s/tests/src:%s/tests/src:%s/src:%s", BUILDDIR, BUILDDIR, SRCDIR, BUILDDIR, pam_getenv (pamh, "PATH"));
    pam_putenv (pamh, e);
    g_free (e);

    if (strcmp (pamh->user, "cred-error") == 0)
        return PAM_CRED_ERR;
    if (strcmp (pamh->user, "cred-expired") == 0)
        return PAM_CRED_EXPIRED;
    if (strcmp (pamh->user, "cred-unavail") == 0)
        return PAM_CRED_UNAVAIL;

    /* Join special groups if requested */
    if (strcmp (pamh->user, "group-member") == 0 && flags & PAM_ESTABLISH_CRED)
    {
        struct group *group;
        gid_t *groups;
        int groups_length;

        group = getgrnam ("test-group");
        if (group)
        {
            groups_length = getgroups (0, NULL);
            if (groups_length < 0)
                return PAM_SYSTEM_ERR;
            groups = malloc (sizeof (gid_t) * (groups_length + 1));
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
    if (pamh == NULL)
        return PAM_SYSTEM_ERR;
  
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
    return (struct utmpx *)ut;
}

void
endutxent (void)
{
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
    xcb_connection_t *c;
    gchar *socket_path;
    GError *error = NULL;
  
    c = malloc (sizeof (xcb_connection_t));
    c->display = g_strdup (display);
    c->error = 0;

    if (display == NULL)
        display = getenv ("DISPLAY");
    if (display == NULL)
        c->error = XCB_CONN_CLOSED_PARSE_ERR;

    if (c->error == 0)
    {
        c->socket = g_socket_new (G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);
        if (error)
            g_printerr ("%s\n", error->message);
        g_clear_error (&error);
        if (c->socket == NULL)
            c->error = XCB_CONN_ERROR;
    }

    if (c->error == 0)
    {
        gchar *d;
        GSocketAddress *address;

        /* Skip the hostname, we'll assume it's localhost */
        d = g_strdup_printf (".x%s", strchr (display, ':'));

        socket_path = g_build_filename (g_getenv ("LIGHTDM_TEST_ROOT"), d, NULL);
        g_free (d);
        address = g_unix_socket_address_new (socket_path);
        if (!g_socket_connect (c->socket, address, NULL, &error))
            c->error = XCB_CONN_ERROR;
        g_object_unref (address);
        if (error)
            g_printerr ("Failed to connect to X socket %s: %s\n", socket_path, error->message);
        g_free (socket_path);
        g_clear_error (&error);
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
        g_object_unref (c->socket);
    free (c);
}
