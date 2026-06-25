#pragma once

#include <Arduino.h>

void detector_log_info(const char *scope, const char *message);
void detector_log_warn(const char *scope, const char *message);
void detector_log_error(const char *scope, const char *message);
void detector_log_infof(const char *scope, const char *format, ...);
void detector_log_warnf(const char *scope, const char *format, ...);
void detector_log_errorf(const char *scope, const char *format, ...);
