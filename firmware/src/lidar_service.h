#pragma once

#include <Arduino.h>

bool lidar_init();
bool lidar_is_ok();
void lidar_set_paused(bool paused);
bool lidar_is_enabled();
void lidar_set_enabled(bool enabled);
bool lidar_start_measure_task();
bool lidar_read_once(uint16_t &distanceMm);
bool lidar_consume_photo_request(uint16_t &distanceMm);

struct LidarStatus
{
    bool lidarOk;
    bool enabled;
    bool baselineReady;
    bool currentValid;
    uint16_t baselineMm;
    uint16_t currentDistanceMm;
    uint16_t thresholdMm;
    uint8_t thresholdPercent;
    uint16_t stableDeltaMm;
    uint16_t goodSampleCount;
    uint16_t sampleDelayMs;
    uint16_t postFrameDelayMs;
};

bool lidar_get_status(LidarStatus &status, bool refreshCurrent = false);
bool lidar_set_base_distance(uint16_t distanceMm);
bool lidar_set_base_to_current(uint16_t &distanceMm);
bool lidar_set_sample_config(uint16_t goodSampleCount, uint16_t sampleDelayMs);
bool lidar_set_post_frame_delay(uint16_t delayMs);
void lidar_mark_lidar_frame_completed();
