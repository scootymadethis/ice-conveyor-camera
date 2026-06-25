#pragma once

#include <Arduino.h>

void log_info(const char *scope, const char *message);
void log_warn(const char *scope, const char *message);
void log_error(const char *scope, const char *message);
void log_infof(const char *scope, const char *format, ...);
void log_warnf(const char *scope, const char *format, ...);
void log_errorf(const char *scope, const char *format, ...);
