#include <string.h>

#include "x-common.h"

gsize
pad (gsize length)
{
    if (length % 4 == 0)
        return 0;
    return 4 - length % 4;
}

void
read_padding (gsize length, gsize *offset)
{
    *offset += length;
}

guint8
read_card8 (const guint8 *buffer, gsize buffer_length, gsize *offset)
{
    if (*offset >= buffer_length)
        return 0;
    (*offset)++;
    return buffer[*offset - 1];
}

guint16
read_card16 (const guint8 *buffer, gsize buffer_length, guint8 byte_order, gsize *offset)
{
    guint8 a, b;

    a = read_card8 (buffer, buffer_length, offset);
    b = read_card8 (buffer, buffer_length, offset);
    if (byte_order == X_BYTE_ORDER_MSB)
        return a << 8 | b;
    else
        return b << 8 | a;
}

guint32
read_card32 (const guint8 *buffer, gsize buffer_length, guint8 byte_order, gsize *offset)
{
    guint8 a, b, c, d;

    a = read_card8 (buffer, buffer_length, offset);
    b = read_card8 (buffer, buffer_length, offset);
    c = read_card8 (buffer, buffer_length, offset);
    d = read_card8 (buffer, buffer_length, offset);
    if (byte_order == X_BYTE_ORDER_MSB)
        return a << 24 | b << 16 | c << 8 | d;
    else
        return d << 24 | c << 16 | b << 8 | a;
}

guint8 *
read_string8 (const guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset)
{
    guint8 *string;
    int i;

    string = g_malloc (string_length + 1);
    for (i = 0; i < string_length; i++)
        string[i] = read_card8 (buffer, buffer_length, offset);
    string[i] = '\0';
    return string;
}

gchar *
read_string (const guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset)
{
    return (gchar *) read_string8 (buffer, buffer_length, string_length, offset);
}

gchar *
read_padded_string (const guint8 *buffer, gsize buffer_length, gsize string_length, gsize *offset)
{
    guint8 *value;
    value = read_string8 (buffer, buffer_length, string_length, offset);
    read_padding (pad (string_length), offset);
    return (gchar *) value;
}

void
write_card8 (guint8 *buffer, gsize buffer_length, guint8 value, gsize *offset)
{
    if (*offset >= buffer_length)
        return;
    buffer[*offset] = value;
    (*offset)++;
}

void
write_padding (guint8 *buffer, gsize buffer_length, gsize length, gsize *offset)
{
    gsize i;
    for (i = 0; i < length; i++)
        write_card8 (buffer, buffer_length, 0, offset);
}

void
write_card16 (guint8 *buffer, gsize buffer_length, guint8 byte_order, guint16 value, gsize *offset)
{
    if (byte_order == X_BYTE_ORDER_MSB)
    {
        write_card8 (buffer, buffer_length, value >> 8, offset);
        write_card8 (buffer, buffer_length, value & 0xFF, offset);
    }
    else
    {
        write_card8 (buffer, buffer_length, value & 0xFF, offset);
        write_card8 (buffer, buffer_length, value >> 8, offset);
    }
}

void
write_card32 (guint8 *buffer, gsize buffer_length, guint8 byte_order, guint32 value, gsize *offset)
{
    if (byte_order == X_BYTE_ORDER_MSB)
    {
        write_card8 (buffer, buffer_length, value >> 24, offset);
        write_card8 (buffer, buffer_length, (value >> 16) & 0xFF, offset);
        write_card8 (buffer, buffer_length, (value >> 8) & 0xFF, offset);
        write_card8 (buffer, buffer_length, value & 0xFF, offset);
    }
    else
    {
        write_card8 (buffer, buffer_length, value & 0xFF, offset);
        write_card8 (buffer, buffer_length, (value >> 8) & 0xFF, offset);
        write_card8 (buffer, buffer_length, (value >> 16) & 0xFF, offset);
        write_card8 (buffer, buffer_length, value >> 24, offset);
    }
}

void
write_string8 (guint8 *buffer, gsize buffer_length, const guint8 *value, gsize value_length, gsize *offset)
{
    gsize i;
    for (i = 0; i < value_length; i++)
        write_card8 (buffer, buffer_length, value[i], offset);
}

gsize
padded_string_length (const gchar *value)
{
    return (strlen (value) + pad (strlen (value))) / 4;
}

void
write_string (guint8 *buffer, gsize buffer_length, const gchar *value, gsize *offset)
{
    write_string8 (buffer, buffer_length, (guint8 *) value, strlen (value), offset);
}

void
write_padded_string (guint8 *buffer, gsize buffer_length, const gchar *value, gsize *offset)
{
    write_string8 (buffer, buffer_length, (guint8 *) value, strlen (value), offset);
    write_padding (buffer, buffer_length, pad (strlen (value)), offset);
}
