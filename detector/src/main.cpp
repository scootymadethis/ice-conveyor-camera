#include <Arduino.h>

#include "detector_config.h"
#include "espnow_receiver.h"
#include "logging.h"
#include "model_runner.h"

void setup()
{
    Serial.begin(DETECTOR_SERIAL_BAUD);
    while (!Serial && millis() < 3000)
    {
        delay(10);
    }

    detector_log_info("boot", "ESP32 object_detection_model starting");

    if (!espnow_receiver_begin())
    {
        detector_log_error("boot", "ESP-NOW receiver failed; reboot after checking Wi-Fi/ESP-NOW setup");
        return;
    }

    if (model_runner_begin())
    {
        model_runner_run_self_test();
        detector_log_info("boot", "ready; waiting for ESP-NOW JPEG frames");
    }
}

void loop()
{
    if (!espnow_frame_available())
    {
        delay(1);
        return;
    }

    ReceivedFrame frame = espnow_current_frame();
    detector_log_infof("frame", "processing frame_id=%u bytes=%u", (unsigned)frame.frame_id, (unsigned)frame.length);
    model_runner_run_frame(frame.data, frame.length);
    espnow_release_frame();
}
