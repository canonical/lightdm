#include <string.h>

#include "x-authority.h"
#include "x-common.h"

struct XAuthorityPrivate
{
    GList *records;
};

struct XAuthorityRecordPrivate
{
    guint16 family;
    guint16 address_length;
    guint8 *address;
    gchar *number;
    gchar *authorization_name;
    guint16 authorization_data_length;
    guint8 *authorization_data;
};

G_DEFINE_TYPE (XAuthority, x_authority, G_TYPE_OBJECT);
G_DEFINE_TYPE (XAuthorityRecord, x_authority_record, G_TYPE_OBJECT);

XAuthority *
x_authority_new (void)
{
    return g_object_new (x_authority_get_type (), NULL);
}

gboolean
x_authority_load (XAuthority *authority, const gchar *filename, GError **error)
{
    guint8 *xauth_data;
    gsize xauth_length;
    gsize offset = 0;

    if (!g_file_get_contents (filename, (gchar **) &xauth_data, &xauth_length, error))
        return FALSE;

    while (offset < xauth_length)
    {
        XAuthorityRecord *record;
        guint16 length;

        record = g_object_new (x_authority_record_get_type (), NULL);
        record->priv->family = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        record->priv->address_length = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        record->priv->address = read_string8 (xauth_data, xauth_length, record->priv->address_length, &offset);
        length = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        record->priv->number = (gchar *) read_string8 (xauth_data, xauth_length, length, &offset);
        length = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        record->priv->authorization_name = (gchar *) read_string8 (xauth_data, xauth_length, length, &offset);
        record->priv->authorization_data_length = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        record->priv->authorization_data = read_string8 (xauth_data, xauth_length, record->priv->authorization_data_length, &offset);

        authority->priv->records = g_list_append (authority->priv->records, record);
    }

    return TRUE;
}

XAuthorityRecord *
x_authority_match_local (XAuthority *authority, const gchar *authorization_name)
{
    GList *link;

    for (link = authority->priv->records; link; link = link->next)
    {
        XAuthorityRecord *record = link->data;

        if (strcmp (record->priv->authorization_name, authorization_name) != 0)
            continue;

        if (record->priv->family == XAUTH_FAMILY_WILD || record->priv->family == XAUTH_FAMILY_LOCAL)
            return record;
    }

    return NULL;
}

XAuthorityRecord *
x_authority_match_localhost (XAuthority *authority, const gchar *authorization_name)
{
    GList *link;

    for (link = authority->priv->records; link; link = link->next)
    {
        XAuthorityRecord *record = link->data;

        if (strcmp (record->priv->authorization_name, authorization_name) != 0)
            continue;

        if (record->priv->family == XAUTH_FAMILY_WILD || record->priv->family == XAUTH_FAMILY_LOCALHOST)
            return record;
    }

    return NULL;
}

XAuthorityRecord *
x_authority_match_inet (XAuthority *authority, GInetAddress *address, const gchar *authorization_name)
{
    GList *link;
    guint16 family;
    gssize address_data_length;
    const guint8 *address_data;

    switch (g_inet_address_get_family (address))
    {
    case G_SOCKET_FAMILY_IPV4:
        family = XAUTH_FAMILY_INTERNET;
        break;
    case G_SOCKET_FAMILY_IPV6:
        family = XAUTH_FAMILY_INTERNET6;
        break;
    default:
        return NULL;
    }

    address_data_length = g_inet_address_get_native_size (address);
    address_data = g_inet_address_to_bytes (address);
    for (link = authority->priv->records; link; link = link->next)
    {
        XAuthorityRecord *record = link->data;
        int i;
        gboolean matches = TRUE;

        if (strcmp (record->priv->authorization_name, authorization_name) != 0)
            continue;

        if (record->priv->family == XAUTH_FAMILY_WILD)
            return record;

        if (record->priv->family != family)
            continue;

        if (record->priv->address_length != address_data_length)
            continue;

        for (i = 0; i < address_data_length; i++)
        {
            if (address_data[i] != record->priv->address[i])
            {
                matches = FALSE;
                break;
            }
        }
        if (matches)
            return record;
    }

    return NULL;
}

static void
x_authority_init (XAuthority *authority)
{
    authority->priv = G_TYPE_INSTANCE_GET_PRIVATE (authority, x_authority_get_type (), XAuthorityPrivate);
}

static void
x_authority_finalize (GObject *object)
{
    XAuthority *authority = (XAuthority *) object;
    g_list_free_full (authority->priv->records, g_object_unref);
}

static void
x_authority_class_init (XAuthorityClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = x_authority_finalize;
    g_type_class_add_private (klass, sizeof (XAuthorityPrivate));
}

guint16
x_authority_record_get_authorization_data_length (XAuthorityRecord *record)
{
    return record->priv->authorization_data_length;
}

const guint8 *
x_authority_record_get_authorization_data (XAuthorityRecord *record)
{
    return record->priv->authorization_data;
}

gboolean
x_authority_record_check_cookie (XAuthorityRecord *record, const guint8 *cookie_data, guint16 cookie_data_length)
{
    guint16 i;

    if (strcmp (record->priv->authorization_name, "MIT-MAGIC-COOKIE-1") != 0)
        return FALSE;

    if (cookie_data_length != record->priv->authorization_data_length)
        return FALSE;

    for (i = 0; i < cookie_data_length; i++)
        if (cookie_data[i] != record->priv->authorization_data[i])
            return FALSE;

    return TRUE;
}

static void
x_authority_record_init (XAuthorityRecord *record)
{
    record->priv = G_TYPE_INSTANCE_GET_PRIVATE (record, x_authority_record_get_type (), XAuthorityRecordPrivate);
}

static void
x_authority_record_finalize (GObject *object)
{
    XAuthorityRecord *record = (XAuthorityRecord *) object;
    g_free (record->priv->address);
    g_free (record->priv->number);
    g_free (record->priv->authorization_name);
    g_free (record->priv->authorization_data);
}

static void
x_authority_record_class_init (XAuthorityRecordClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = x_authority_record_finalize;
    g_type_class_add_private (klass, sizeof (XAuthorityRecordPrivate));
}
