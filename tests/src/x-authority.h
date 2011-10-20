#ifndef _X_AUTHORITY_H_
#define _X_AUTHORITY_H_

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

enum
{
    XAUTH_FAMILY_INTERNET = 0,
    XAUTH_FAMILY_DECNET = 1,
    XAUTH_FAMILY_CHAOS = 2,
    XAUTH_FAMILY_SERVER_INTERPRETED = 5,
    XAUTH_FAMILY_INTERNET6 = 6,
    XAUTH_FAMILY_LOCALHOST = 252,
    XAUTH_FAMILY_KRB5_PRINCIPAL = 253,
    XAUTH_FAMILY_NETNAME = 254,
    XAUTH_FAMILY_LOCAL = 256,
    XAUTH_FAMILY_WILD = 65535
};

typedef struct XAuthorityPrivate XAuthorityPrivate;

typedef struct
{
    GObjectClass parent_instance;
    XAuthorityPrivate *priv;
} XAuthority;

typedef struct
{
    GObjectClass parent_class;
} XAuthorityClass;

typedef struct XAuthorityRecordPrivate XAuthorityRecordPrivate;

typedef struct
{
    GObjectClass parent_instance;
    XAuthorityRecordPrivate *priv;
} XAuthorityRecord;

typedef struct
{
    GObjectClass parent_class;
} XAuthorityRecordClass;

GType x_authority_get_type (void);

GType x_authority_record_get_type (void);

XAuthority *x_authority_new (void);

gboolean x_authority_load (XAuthority *authority, const gchar *filename, GError **error);

XAuthorityRecord *x_authority_match_local (XAuthority *authority, const gchar *authorization_name);

XAuthorityRecord *x_authority_match_localhost (XAuthority *authority, const gchar *authorization_name);

XAuthorityRecord *x_authority_match_inet (XAuthority *authority, GInetAddress *address, const gchar *authorization_name);

guint16 x_authority_record_get_authorization_data_length (XAuthorityRecord *record);

const guint8 *x_authority_record_get_authorization_data (XAuthorityRecord *record);

gboolean x_authority_record_check_cookie (XAuthorityRecord *record, const guint8 *cookie_data, guint16 cookie_data_length);

G_END_DECLS

#endif /* _X_AUTHORITY_H_ */
