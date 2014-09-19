#ifndef STATUS_H_
#define STATUS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <glib-object.h>

typedef void (*StatusRequestFunc)(const gchar *name, GHashTable *params);

gboolean status_connect (StatusRequestFunc message_cb, const gchar *id);

void status_notify (const gchar *format, ...) __attribute__((format (printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif /* STATUS_H_ */
