#include <string.h>

#include "x-authority.h"
#include "x-common.h"

typedef struct
{
    GList *records;
} XAuthorityPrivate;

typedef struct
{
    guint16 family;
    guint16 address_length;
    guint8 *address;
    gchar *number;
    gchar *authorization_name;
    guint16 authorization_data_length;
    guint8 *authorization_data;
} XAuthorityRecordPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (XAuthority, x_authority, G_TYPE_OBJECT)
G_DEFINE_TYPE_WITH_PRIVATE (XAuthorityRecord, x_authority_record, G_TYPE_OBJECT)

XAuthority *
x_authority_new (void)
{
    return g_object_new (x_authority_get_type (), NULL);
}

gboolean
x_authority_load (XAuthority *authority, const gchar *filename, GError **error)
{
    XAuthorityPrivate *priv = x_authority_get_instance_private (authority);

    guint8 *xauth_data;
    gsize xauth_length;
    if (!g_file_get_contents (filename, (gchar **) &xauth_data, &xauth_length, error))
        return FALSE;

    gsize offset = 0;
    while (offset < xauth_length)
    {
        XAuthorityRecord *record = g_object_new (x_authority_record_get_type (), NULL);
        XAuthorityRecordPrivate *r_priv = x_authority_record_get_instance_private (record);

        r_priv->family = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        r_priv->address_length = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        r_priv->address = read_string8 (xauth_data, xauth_length, r_priv->address_length, &offset);
        guint16 length = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        r_priv->number = (gchar *) read_string8 (xauth_data, xauth_length, length, &offset);
        length = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        r_priv->authorization_name = (gchar *) read_string8 (xauth_data, xauth_length, length, &offset);
        r_priv->authorization_data_length = read_card16 (xauth_data, xauth_length, X_BYTE_ORDER_MSB, &offset);
        r_priv->authorization_data = read_string8 (xauth_data, xauth_length, r_priv->authorization_data_length, &offset);

        priv->records = g_list_append (priv->records, record);
    }

    return TRUE;
}

XAuthorityRecord *
x_authority_match_local (XAuthority *authority, const gchar *authorization_name)
{
    XAuthorityPrivate *priv = x_authority_get_instance_private (authority);

    for (GList *link = priv->records; link; link = link->next)
    {
        XAuthorityRecord *record = link->data;
        XAuthorityRecordPrivate *r_priv = x_authority_record_get_instance_private (record);

        if (strcmp (r_priv->authorization_name, authorization_name) != 0)
            continue;

        if (r_priv->family == XAUTH_FAMILY_WILD || r_priv->family == XAUTH_FAMILY_LOCAL)
            return record;
    }

    return NULL;
}

XAuthorityRecord *
x_authority_match_localhost (XAuthority *authority, const gchar *authorization_name)
{
    XAuthorityPrivate *priv = x_authority_get_instance_private (authority);

    for (GList *link = priv->records; link; link = link->next)
    {
        XAuthorityRecord *record = link->data;
        XAuthorityRecordPrivate *r_priv = x_authority_record_get_instance_private (record);

        if (strcmp (r_priv->authorization_name, authorization_name) != 0)
            continue;

        if (r_priv->family == XAUTH_FAMILY_WILD || r_priv->family == XAUTH_FAMILY_LOCALHOST)
            return record;
    }

    return NULL;
}

XAuthorityRecord *
x_authority_match_inet (XAuthority *authority, GInetAddress *address, const gchar *authorization_name)
{
    XAuthorityPrivate *priv = x_authority_get_instance_private (authority);

    guint16 family;
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

    gssize address_data_length = g_inet_address_get_native_size (address);
    const guint8 *address_data = g_inet_address_to_bytes (address);
    for (GList *link = priv->records; link; link = link->next)
    {
        XAuthorityRecord *record = link->data;
        XAuthorityRecordPrivate *r_priv = x_authority_record_get_instance_private (record);

        if (strcmp (r_priv->authorization_name, authorization_name) != 0)
            continue;

        if (r_priv->family == XAUTH_FAMILY_WILD)
            return record;

        if (r_priv->family != family)
            continue;

        if (r_priv->address_length != address_data_length)
            continue;

        gboolean matches = TRUE;
        for (int i = 0; i < address_data_length; i++)
        {
            if (address_data[i] != r_priv->address[i])
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
}

static void
x_authority_finalize (GObject *object)
{
    XAuthority *authority = (XAuthority *) object;
    XAuthorityPrivate *priv = x_authority_get_instance_private (authority);

    g_list_free_full (priv->records, g_object_unref);
}

static void
x_authority_class_init (XAuthorityClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = x_authority_finalize;
}

guint16
x_authority_record_get_authorization_data_length (XAuthorityRecord *record)
{
    XAuthorityRecordPrivate *priv = x_authority_record_get_instance_private (record);
    return priv->authorization_data_length;
}

const guint8 *
x_authority_record_get_authorization_data (XAuthorityRecord *record)
{
    XAuthorityRecordPrivate *priv = x_authority_record_get_instance_private (record);
    return priv->authorization_data;
}

gboolean
x_authority_record_check_cookie (XAuthorityRecord *record, const guint8 *cookie_data, guint16 cookie_data_length)
{
    XAuthorityRecordPrivate *priv = x_authority_record_get_instance_private (record);

    if (strcmp (priv->authorization_name, "MIT-MAGIC-COOKIE-1") != 0)
        return FALSE;

    if (cookie_data_length != priv->authorization_data_length)
        return FALSE;

    for (guint16 i = 0; i < cookie_data_length; i++)
        if (cookie_data[i] != priv->authorization_data[i])
            return FALSE;

    return TRUE;
}

static void
x_authority_record_init (XAuthorityRecord *record)
{
}

static void
x_authority_record_finalize (GObject *object)
{
    XAuthorityRecord *record = (XAuthorityRecord *) object;
    XAuthorityRecordPrivate *priv = x_authority_record_get_instance_private (record);

    g_free (priv->address);
    g_free (priv->number);
    g_free (priv->authorization_name);
    g_free (priv->authorization_data);
}

static void
x_authority_record_class_init (XAuthorityRecordClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = x_authority_record_finalize;
}
