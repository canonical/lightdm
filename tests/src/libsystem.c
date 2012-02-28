#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <unistd.h>
#define __USE_GNU
#include <dlfcn.h>
#ifdef __linux__
#include <linux/vt.h>
#endif
#include <glib.h>

#define LOGIN_PROMPT "login:"

static int console_fd = -1;

static GList *user_entries = NULL;
static GList *getpwent_link = NULL;

struct pam_handle
{
    char *service_name;
    char *user;
    char *tty;
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
    return 0;
}

int
setgid (gid_t gid)
{
    return 0;
}

int
setuid (uid_t uid)
{
    return 0;
}

#ifdef __linux__
int
open (const char *pathname, int flags, mode_t mode)
{
    int (*_open) (const char * pathname, int flags, mode_t mode);

    _open = (int (*)(const char * pathname, int flags, mode_t mode)) dlsym (RTLD_NEXT, "open");      
    if (strcmp (pathname, "/dev/console") == 0)
    {
        if (console_fd < 0)
            console_fd = _open ("/dev/null", 0, 0);
        return console_fd;
    }
    else
        return _open(pathname, flags, mode);
}

int
ioctl (int d, int request, void *data)
{
    int (*_ioctl) (int d, int request, void *data);

    _ioctl = (int (*)(int d, int request, void *data)) dlsym (RTLD_NEXT, "ioctl");
    if (d > 0 && d == console_fd)
    {
        struct vt_stat *console_state;

        switch (request)
        {
        case VT_GETSTATE:
            console_state = data;
            console_state->v_active = 7;
            break;          
        case VT_ACTIVATE:
            break;
        }
        return 0;
    }
    else
        return _ioctl (d, request, data);
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
load_passwd_file ()
{
    gchar *data = NULL, **lines;
    gint i;
    GError *error = NULL;

    g_list_free_full (user_entries, free_user);
    user_entries = NULL;
    getpwent_link = NULL;

    g_file_get_contents (g_getenv ("LIGHTDM_TEST_PASSWD_FILE"), &data, NULL, &error);
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
    handle->tty = NULL;
    handle->conversation.conv = conversation->conv;
    handle->conversation.appdata_ptr = conversation->appdata_ptr;

    return PAM_SUCCESS;
}

int
pam_authenticate (pam_handle_t *pamh, int flags)
{
    struct passwd *entry;
    gboolean password_matches = FALSE;

    if (pamh == NULL)
        return PAM_SYSTEM_ERR;

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

    /* Look up password database */
    entry = getpwnam (pamh->user);

    /* Prompt for password if required */
    if (entry && (strcmp (pamh->service_name, "lightdm-autologin") == 0 || strcmp (entry->pw_passwd, "") == 0))
        password_matches = TRUE;
    else
    {
        int result;
        struct pam_message **msg;
        struct pam_response *resp = NULL;
    
        msg = malloc (sizeof (struct pam_message *) * 1);
        msg[0] = malloc (sizeof (struct pam_message));
        msg[0]->msg_style = PAM_PROMPT_ECHO_OFF;
        msg[0]->msg = "Password:";
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

        if (entry)
            password_matches = strcmp (entry->pw_passwd, resp[0].resp) == 0;
        free (resp[0].resp);  
        free (resp);
    }

    /* Special user has home directory created on login */
    if (password_matches && strcmp (pamh->user, "mount-home-dir") == 0)
        g_mkdir_with_parents (entry->pw_dir, 0755);

    /* Special user 'user0' changes user on authentication */
    if (password_matches && strcmp (pamh->user, "user0") == 0)
    {
        g_free (pamh->user);
        pamh->user = g_strdup ("user1");
    }

    /* Special user 'rename-user-invalid' changes to an invalid user on authentication */
    if (password_matches && strcmp (pamh->user, "rename-user-invalid") == 0)
    {
        g_free (pamh->user);
        pamh->user = g_strdup ("invalid-user");
    }

    if (password_matches)
        return PAM_SUCCESS;
    else
        return PAM_AUTH_ERR;
}

const char *
pam_getenv (pam_handle_t *pamh, const char *name)
{
    if (pamh == NULL || name == NULL)
        return NULL;

    if (strcmp (name, "PATH") == 0)
        return getenv ("PATH");

    return NULL;
}

char **
pam_getenvlist (pam_handle_t *pamh)
{
    char **envlist;

    if (pamh == NULL)
        return NULL;

    envlist = malloc (sizeof (char *) * 2);
    envlist[0] = g_strdup_printf ("PATH=%s", getenv ("PATH"));
    envlist[1] = NULL;

    return envlist;
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

    return PAM_SUCCESS;
}

int
pam_close_session (pam_handle_t *pamh, int flags)
{
    if (pamh == NULL)
        return PAM_SYSTEM_ERR;

    return PAM_SUCCESS;
}

int
pam_acct_mgmt (pam_handle_t *pamh, int flags)
{
    if (pamh == NULL)
        return PAM_SYSTEM_ERR;
  
    if (!pamh->user)
        return PAM_USER_UNKNOWN;

    if (strcmp (pamh->user, "denied") == 0)
        return PAM_PERM_DENIED;
    if (strcmp (pamh->user, "expired") == 0)
        return PAM_ACCT_EXPIRED;

    return PAM_SUCCESS;
}

int
pam_chauthtok (pam_handle_t *pamh, int flags)
{
    if (pamh == NULL)
        return PAM_SYSTEM_ERR;

    return PAM_SUCCESS;
}

int
pam_setcred (pam_handle_t *pamh, int flags)
{
    if (pamh == NULL)
        return PAM_SYSTEM_ERR;

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
