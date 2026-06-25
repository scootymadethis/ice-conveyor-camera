#include "logging.h"

#include <stdarg.h>

static void print_prefix(const char *level, const char *scope)
{
    Serial.printf("[%lu ms][%s][%s] ", millis(), level, scope ? scope : "detector");
}

static void print_line(const char *level, const char *scope, const char *message)
{
    print_prefix(level, scope);
    Serial.println(message ? message : "");
}

static void print_vline(const char *level, const char *scope, const char *format, va_list args)
{
    print_prefix(level, scope);
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format ? format : "", args);
    Serial.print(buffer);
    Serial.println();
}

void detector_log_info(const char *scope, const char *message) { print_line("INFO", scope, message); }
void detector_log_warn(const char *scope, const char *message) { print_line("WARN", scope, message); }
void detector_log_error(const char *scope, const char *message) { print_line("ERROR", scope, message); }

void detector_log_infof(const char *scope, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    print_vline("INFO", scope, format, args);
    va_end(args);
}

void detector_log_warnf(const char *scope, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    print_vline("WARN", scope, format, args);
    va_end(args);
}

void detector_log_errorf(const char *scope, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    print_vline("ERROR", scope, format, args);
    va_end(args);
}
