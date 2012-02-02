#ifndef _STATUS_H_
#define _STATUS_H_

#ifdef __cplusplus
extern "C" {
#endif
  
#include <glib-object.h>

typedef void (*StatusRequestFunc)(const gchar *message);

void status_connect (StatusRequestFunc message_cb);

void status_notify (const gchar *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* _STATUS_H_ */
