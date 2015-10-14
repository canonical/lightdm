#include "logger.h"

G_DEFINE_INTERFACE (Logger, logger, G_TYPE_INVALID);

static void
logger_logv_default (Logger *self, GLogLevelFlags log_level, const gchar *format, va_list ap) __attribute__ ((format (printf, 3, 0)));

static void
logger_default_init (LoggerInterface *iface)
{
    iface->logv = &logger_logv_default;
}

gint
logger_logprefix (Logger *self, gchar *buf, gulong buflen)
{
    g_return_val_if_fail (IS_LOGGER (self), -1);
    return LOGGER_GET_INTERFACE (self)->logprefix (self, buf, buflen);
}

void
logger_logv (Logger *self, GLogLevelFlags log_level, const gchar *format, va_list ap)
{
    g_return_if_fail (IS_LOGGER (self));
    LOGGER_GET_INTERFACE (self)->logv (self, log_level, format, ap);
}

void
logger_logv_default (Logger *self, GLogLevelFlags log_level, const gchar *format, va_list ap)
{
    va_list ap_copy;
    gint tmp;

    /* figure out how long the prefix is */
    tmp = logger_logprefix (self, NULL, 0);
    if (tmp < 0)
    {
        g_error ("failed to get log prefix");
        return;
    }

    /* print the prefix to a variable length array (to avoid malloc) */
    gchar pfx[tmp + 1];
    tmp = logger_logprefix (self, pfx, sizeof(pfx));
    if (tmp < 0)
    {
        g_error ("failed to get log prefix");
        return;
    }

    /* figure out how long the formatted message is */
    va_copy (ap_copy, ap);
    tmp = g_vsnprintf (NULL, 0, format, ap_copy);
    va_end (ap_copy);
    if (tmp < 0)
    {
        g_error ("failed to format log message");
        return;
    }

    /* print the message to a variable length array (to avoid malloc) */
    gchar msg[tmp+1];
    tmp = g_vsnprintf (msg, sizeof(msg), format, ap);
    if (tmp < 0)
    {
        g_error ("failed to format log message");
        return;
    }

    /* log the message with the prefix */
    g_log (G_LOG_DOMAIN, log_level, "%s%s", pfx, msg);
}

void
logger_log (Logger *self, GLogLevelFlags log_level, const gchar *format, ...)
{
    va_list ap;
    va_start (ap, format);
    logger_logv (self, log_level, format, ap);
    va_end (ap);
}
