#ifndef X_COMMON_H_
#define X_COMMON_H_

#include <glib-object.h>

G_BEGIN_DECLS

enum
{
    X_BYTE_ORDER_MSB,
    X_BYTE_ORDER_LSB
};

gsize pad (gsize length);

void read_padding (gsize length, gsize *offset);

guint8 read_card8 (const guint8 *buffer, gsize buffer_length, gsize *offset);

guint16 read_card16 (const guint8 *buffer, gsize buffer_length, guint8 byte_order, gsize *offset);

guint32 read_card32 (const guint8 *buffer, gsize buffer_length, guint8 byte_order, gsize *offset);

guint8 *read_string8 (const guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset);

gchar *read_string (const guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset);

gchar *read_padded_string (const guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset);

void write_card8 (guint8 *buffer, gsize buffer_length, guint8 value, gsize *offset);

void write_padding (guint8 *buffer, gsize buffer_length, gsize length, gsize *offset);

void write_card16 (guint8 *buffer, gsize buffer_length, guint8 byte_order, guint16 value, gsize *offset);

void write_card32 (guint8 *buffer, gsize buffer_length, guint8 byte_order, guint32 value, gsize *offset);

void write_string8 (guint8 *buffer, gsize buffer_length, const guint8 *value, gsize value_length, gsize *offset);

gsize padded_string_length (const gchar *value);

void write_string (guint8 *buffer, gsize buffer_length, const gchar *value, gsize *offset);

void write_padded_string (guint8 *buffer, gsize buffer_length, const gchar *value, gsize *offset);

G_END_DECLS

#endif /* X_COMMON_H_ */
