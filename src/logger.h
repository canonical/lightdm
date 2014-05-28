#ifndef LOGGER_H_
#define LOGGER_H_

#include <glib-object.h>

#ifdef __cplusplus
#include <cstdarg>  /* for va_list */
#else
#include <stdarg.h>  /* for va_list */
#endif

G_BEGIN_DECLS

#define LOGGER_TYPE (logger_get_type ())
#define LOGGER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), LOGGER_TYPE, Logger))
#define IS_LOGGER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LOGGER_TYPE))
#define LOGGER_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), LOGGER_TYPE, LoggerInterface))

typedef struct Logger Logger;

typedef struct {
    GTypeInterface parent;

    gint (*logprefix) (Logger *self, gchar *buf, gulong buflen);
    void (*logv) (Logger *self, GLogLevelFlags log_level, const gchar *format, va_list ap);
} LoggerInterface;

GType logger_get_type (void);

/*!
 * \brief instruct \c self to generate a log message prefix
 *
 * the semantics of the \c buf and \c buflen arguments and the return
 * value are the same as g_snprintf()
 *
 * there is no default implementation
 */
gint logger_logprefix (Logger *self, gchar *buf, gulong buflen);

/*!
 * \brief instruct \c self to log the given message
 *
 * the default implementation prefixes the log message with the
 * output of logger_logprefix() and then passes the result to
 * g_log()
 */
void logger_logv (Logger *self, GLogLevelFlags log_level, const gchar *format, va_list ap) __attribute__ ((format (printf, 3, 0)));

/*! \brief convenience wrapper around \c logger_logv() */
void logger_log (Logger *self, GLogLevelFlags log_level, const gchar *format, ...) __attribute__ ((format (printf, 3, 4)));

/* convenience wrappers around logger_log() */
#define l_debug(self, ...) \
    logger_log (LOGGER (self), G_LOG_LEVEL_DEBUG, __VA_ARGS__)
#define l_warning(self, ...) \
    logger_log (LOGGER (self), G_LOG_LEVEL_WARNING, __VA_ARGS__)

G_END_DECLS

#endif /* !LOGGER_H_ */
