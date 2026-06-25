#pragma once

#include <Arduino.h>

bool display_init();

void display_clear();
void display_show_status(const String &line1, const String &line2 = "", const String &line3 = "");
void display_show_ip(const String &ip);
void display_show_error(const String &message);