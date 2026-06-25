#pragma once

#include <Arduino.h>

bool model_runner_begin();
bool model_runner_is_ready();
void model_runner_run_frame(const uint8_t *buffer, uint32_t length);
void model_runner_run_self_test();
