#include "display_service.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#include "app_log.h"

static constexpr int SCREEN_WIDTH = 128;
static constexpr int SCREEN_HEIGHT = 64;
static constexpr int OLED_RESET = -1;
static constexpr uint8_t OLED_ADDRESS = 0x3C;

static Adafruit_SH1106G display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET);

static bool displayReady = false;

bool display_init()
{
    // Non chiamare Wire.begin() qui.
    // Il bus I2C è già inizializzato dal lidar_init() su GPIO1/GPIO2.

    if (!display.begin(OLED_ADDRESS, true))
    {
        log_error("display", "SH1106 OLED init failed at I2C address 0x3C");
        displayReady = false;
        return false;
    }

    displayReady = true;

    log_info("display", "SH1106 OLED initialized at I2C address 0x3C");
    return true;
}

void display_clear()
{
    if (!displayReady)
    {
        return;
    }

    display.clearDisplay();
    display.display();
}

void display_show_status(const String &line1, const String &line2, const String &line3)
{
    if (!displayReady)
    {
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    display.setCursor(0, 0);
    display.println(line1);

    if (line2.length() > 0)
    {
        display.setCursor(0, 16);
        display.println(line2);
    }

    if (line3.length() > 0)
    {
        display.setCursor(0, 32);
        display.println(line3);
    }

    display.display();
}

void display_show_ip(const String &ip)
{
    display_show_status("ESP32 online", "IP:", ip);
}

void display_show_error(const String &message)
{
    display_show_status("ERROR", message);
}