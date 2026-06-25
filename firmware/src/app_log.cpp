#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>

static void log_prefix(const char *level, const char *scope)
{
    Serial.printf("[%lu ms][%s][%s] ", millis(), level, scope ? scope : "app");
}

static void log_line(const char *level, const char *scope, const char *message)
{
    log_prefix(level, scope);
    Serial.println(message ? message : "");
}

static void log_vprintf(const char *level, const char *scope, const char *format, va_list args)
{
    log_prefix(level, scope);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format ? format : "", args);
    Serial.print(buffer);
    Serial.println();
}

void log_info(const char *scope, const char *message)
{
    log_line("INFO", scope, message);
}

void log_warn(const char *scope, const char *message)
{
    log_line("WARN", scope, message);
}

void log_error(const char *scope, const char *message)
{
    log_line("ERROR", scope, message);
}

void log_infof(const char *scope, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_vprintf("INFO", scope, format, args);
    va_end(args);
}

void log_warnf(const char *scope, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_vprintf("WARN", scope, format, args);
    va_end(args);
}

void log_errorf(const char *scope, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    log_vprintf("ERROR", scope, format, args);
    va_end(args);
}
