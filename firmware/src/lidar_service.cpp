#include "lidar_service.h"
#include "display_service.h"

#include <Wire.h>
#include "Adafruit_VL53L0X.h"

#include "app_config.h"
#include "app_log.h"
#include "lidar_pins.h"

// Use Arduino I2C controller 0. The ESP32 camera SCCB driver uses the other
// hardware I2C controller on this board; using TwoWire(1) here makes
// esp_camera_init() fail with "i2c driver install error" / "sccb init err".
static Adafruit_VL53L0X lox;
static bool gLidarOk = false;
static volatile bool gPaused = false;
static volatile bool gEnabled = true;
static volatile bool gPhotoRequestPending = false;
static uint16_t gPhotoRequestDistanceMm = 0;
static SemaphoreHandle_t gLidarMutex = nullptr;
static portMUX_TYPE gRequestMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE gBaselineMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE gSampleConfigMux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE gTriggerDelayMux = portMUX_INITIALIZER_UNLOCKED;
static bool gBaselineReady = false;
static uint16_t gBaselineMm = 0;
static bool gCurrentDistanceValid = false;
static uint16_t gCurrentDistanceMm = 0;
static uint16_t gGoodSampleCount = LIDAR_GOOD_SAMPLE_COUNT_DEFAULT;
static uint16_t gSampleDelayMs = LIDAR_SAMPLE_DELAY_MS_DEFAULT;
static uint16_t gPostFrameDelayMs = LIDAR_POST_FRAME_DELAY_MS_DEFAULT;
static uint32_t gBlockTriggersUntilMs = 0;
static bool is_baseline_sample(uint16_t distanceMm)
{
    return distanceMm > 0;
}

static bool is_good_measurement_sample(uint16_t distanceMm)
{
    return distanceMm > 0 && distanceMm < LIDAR_OUT_OF_RANGE_DISTANCE_MM;
}

static bool is_trigger_distance(uint16_t distanceMm)
{
    return is_good_measurement_sample(distanceMm);
}

static uint16_t distance_delta(uint16_t a, uint16_t b)
{
    return a > b ? a - b : b - a;
}

static uint16_t baseline_threshold_mm(uint16_t baselineMm)
{
    return (uint16_t)(((uint32_t)baselineMm * LIDAR_THRESHOLD_PERCENT + 50) / 100);
}

static uint16_t update_tracked_baseline(uint16_t baselineMm, uint16_t distanceMm)
{
    uint32_t weightedSum =
        ((uint32_t)baselineMm * (LIDAR_BASELINE_TRACK_DIVISOR - 1)) +
        distanceMm +
        (LIDAR_BASELINE_TRACK_DIVISOR / 2);

    return (uint16_t)(weightedSum / LIDAR_BASELINE_TRACK_DIVISOR);
}

static void store_current_distance(uint16_t distanceMm)
{
    portENTER_CRITICAL(&gBaselineMux);
    gCurrentDistanceMm = distanceMm;
    gCurrentDistanceValid = is_good_measurement_sample(distanceMm);
    portEXIT_CRITICAL(&gBaselineMux);
}

static void store_current_distance_invalid()
{
    portENTER_CRITICAL(&gBaselineMux);
    gCurrentDistanceValid = false;
    portEXIT_CRITICAL(&gBaselineMux);
}

static void store_baseline(uint16_t distanceMm)
{
    portENTER_CRITICAL(&gBaselineMux);
    gBaselineMm = distanceMm;
    gBaselineReady = true;
    portEXIT_CRITICAL(&gBaselineMux);
}

static bool load_baseline(uint16_t &distanceMm)
{
    bool ready = false;

    portENTER_CRITICAL(&gBaselineMux);
    ready = gBaselineReady;
    distanceMm = gBaselineMm;
    portEXIT_CRITICAL(&gBaselineMux);

    return ready;
}

static void load_sample_config(uint16_t &goodSampleCount, uint16_t &sampleDelayMs)
{
    portENTER_CRITICAL(&gSampleConfigMux);
    goodSampleCount = gGoodSampleCount;
    sampleDelayMs = gSampleDelayMs;
    portEXIT_CRITICAL(&gSampleConfigMux);
}

static uint16_t load_post_frame_delay_ms()
{
    uint16_t delayMs = 0;
    portENTER_CRITICAL(&gTriggerDelayMux);
    delayMs = gPostFrameDelayMs;
    portEXIT_CRITICAL(&gTriggerDelayMux);
    return delayMs;
}

static bool triggers_are_blocked(uint32_t now)
{
    bool blocked = false;
    portENTER_CRITICAL(&gTriggerDelayMux);
    blocked = (int32_t)(gBlockTriggersUntilMs - now) > 0;
    portEXIT_CRITICAL(&gTriggerDelayMux);
    return blocked;
}

static void track_baseline(uint16_t distanceMm)
{
    portENTER_CRITICAL(&gBaselineMux);
    if (gBaselineReady)
    {
        gBaselineMm = update_tracked_baseline(gBaselineMm, distanceMm);
    }
    portEXIT_CRITICAL(&gBaselineMux);
}

static bool read_raw_locked(uint16_t &distanceMm, uint8_t *rangeStatus = nullptr)
{
    distanceMm = 0;

    if (rangeStatus)
    {
        *rangeStatus = 255;
    }

    if (!gLidarOk)
    {
        return false;
    }

    if (gLidarMutex && xSemaphoreTake(gLidarMutex, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        return false;
    }

    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);

    if (rangeStatus)
    {
        *rangeStatus = measure.RangeStatus;
    }

    bool ok = false;

    // RangeStatus 4 means out of range / phase fail in the Adafruit examples.
    if (measure.RangeStatus == 4)
    {
        distanceMm = LIDAR_OUT_OF_RANGE_DISTANCE_MM;
        ok = true;
    }
    else
    {
        distanceMm = measure.RangeMilliMeter;
        ok = true;
    }

    if (gLidarMutex)
    {
        xSemaphoreGive(gLidarMutex);
    }

    return ok;
}

static bool read_good_average(uint16_t &distanceMm, uint16_t *validSamples = nullptr)
{
    distanceMm = 0;

    uint16_t sampleCount = 1;
    uint16_t sampleDelayMs = 0;
    load_sample_config(sampleCount, sampleDelayMs);

    uint32_t sum = 0;
    uint16_t accepted = 0;

    for (uint16_t i = 0; i < sampleCount; i++)
    {
        uint16_t rawDistanceMm = 0;
        uint8_t rangeStatus = 255;

        if (read_raw_locked(rawDistanceMm, &rangeStatus) && is_good_measurement_sample(rawDistanceMm))
        {
            sum += rawDistanceMm;
            accepted++;
        }

        if (i + 1 < sampleCount && sampleDelayMs > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(sampleDelayMs));
        }
    }

    if (validSamples)
    {
        *validSamples = accepted;
    }

    if (accepted == 0)
    {
        return false;
    }

    distanceMm = (uint16_t)((sum + (accepted / 2)) / accepted);
    return true;
}

bool lidar_init()
{
    log_info("lidar", "initializing VL53L0X I2C sensor");

    if (!gLidarMutex)
    {
        gLidarMutex = xSemaphoreCreateMutex();
    }

    Wire.end();
    delay(100);

    if (!Wire.begin(I2C_SDA, I2C_SCL, 100000))
    {
        log_errorf("lidar", "I2C bus init failed sda=%d scl=%d", I2C_SDA, I2C_SCL);
        gLidarOk = false;
        return false;
    }

    delay(300);

    if (!lox.begin(0x29, false, &Wire))
    {
        log_error("lidar", "VL53L0X not found at address 0x29");
        gLidarOk = false;
        return false;
    }

    log_infof("lidar", "VL53L0X found address=0x29 sda=%d scl=%d", I2C_SDA, I2C_SCL);
    gLidarOk = true;
    return true;
}

bool lidar_is_ok()
{
    return gLidarOk;
}

void lidar_set_paused(bool paused)
{
    gPaused = paused;
}

bool lidar_is_enabled()
{
    return gEnabled;
}

void lidar_set_enabled(bool enabled)
{
    gEnabled = enabled;
}

bool lidar_read_once(uint16_t &distanceMm)
{
    bool ok = read_good_average(distanceMm);

    if (ok)
    {
        store_current_distance(distanceMm);
    }

    return ok;
}

bool lidar_get_status(LidarStatus &status, bool refreshCurrent)
{
    if (refreshCurrent && gLidarOk)
    {
        uint16_t distanceMm = 0;

        if (read_good_average(distanceMm))
        {
            store_current_distance(distanceMm);
        }
        else
        {
            store_current_distance_invalid();
        }
    }

    portENTER_CRITICAL(&gBaselineMux);
    status.lidarOk = gLidarOk;
    status.enabled = gEnabled;
    status.baselineReady = gBaselineReady;
    status.currentValid = gCurrentDistanceValid;
    status.baselineMm = gBaselineMm;
    status.currentDistanceMm = gCurrentDistanceMm;
    status.thresholdMm = gBaselineReady ? baseline_threshold_mm(gBaselineMm) : 0;
    status.thresholdPercent = LIDAR_THRESHOLD_PERCENT;
    status.stableDeltaMm = LIDAR_BASELINE_STABLE_DELTA_MM;
    portEXIT_CRITICAL(&gBaselineMux);

    load_sample_config(status.goodSampleCount, status.sampleDelayMs);
    status.postFrameDelayMs = load_post_frame_delay_ms();

    return status.lidarOk;
}

bool lidar_set_base_distance(uint16_t distanceMm)
{
    if (!is_baseline_sample(distanceMm))
    {
        return false;
    }

    store_baseline(distanceMm);
    log_infof("lidar", "baseline stored distance_mm=%u", distanceMm);
    return true;
}

bool lidar_set_base_to_current(uint16_t &distanceMm)
{
    if (!read_good_average(distanceMm))
    {
        store_current_distance_invalid();
        return false;
    }

    store_current_distance(distanceMm);
    return lidar_set_base_distance(distanceMm);
}

bool lidar_set_sample_config(uint16_t goodSampleCount, uint16_t sampleDelayMs)
{
    if (goodSampleCount < LIDAR_GOOD_SAMPLE_COUNT_MIN ||
        goodSampleCount > LIDAR_GOOD_SAMPLE_COUNT_MAX ||
        sampleDelayMs > LIDAR_SAMPLE_DELAY_MS_MAX)
    {
        return false;
    }

    portENTER_CRITICAL(&gSampleConfigMux);
    gGoodSampleCount = goodSampleCount;
    gSampleDelayMs = sampleDelayMs;
    portEXIT_CRITICAL(&gSampleConfigMux);

    log_infof("lidar", "sample config stored good_sample_count=%u sample_delay_ms=%u", goodSampleCount, sampleDelayMs);
    return true;
}


bool lidar_set_post_frame_delay(uint16_t delayMs)
{
    if (delayMs < LIDAR_POST_FRAME_DELAY_MS_MIN || delayMs > LIDAR_POST_FRAME_DELAY_MS_MAX)
    {
        return false;
    }

    portENTER_CRITICAL(&gTriggerDelayMux);
    gPostFrameDelayMs = delayMs;
    portEXIT_CRITICAL(&gTriggerDelayMux);

    log_infof("lidar", "post-frame trigger delay stored delay_ms=%u", delayMs);
    return true;
}

void lidar_mark_lidar_frame_completed()
{
    const uint16_t delayMs = load_post_frame_delay_ms();
    const uint32_t until = millis() + delayMs;
    bool clearedPending = false;

    portENTER_CRITICAL(&gTriggerDelayMux);
    gBlockTriggersUntilMs = until;
    portEXIT_CRITICAL(&gTriggerDelayMux);

    portENTER_CRITICAL(&gRequestMux);
    if (gPhotoRequestPending)
    {
        gPhotoRequestPending = false;
        clearedPending = true;
    }
    portEXIT_CRITICAL(&gRequestMux);

    if (delayMs > 0)
    {
        log_infof("lidar", "post-frame trigger holdoff active delay_ms=%u", delayMs);
    }
    if (clearedPending)
    {
        log_warn("lidar", "cleared stale queued trigger after frame completion");
    }
}

bool lidar_consume_photo_request(uint16_t &distanceMm)
{
    bool hasRequest = false;

    portENTER_CRITICAL(&gRequestMux);
    if (gPhotoRequestPending)
    {
        distanceMm = gPhotoRequestDistanceMm;
        gPhotoRequestPending = false;
        hasRequest = true;
    }
    portEXIT_CRITICAL(&gRequestMux);

    if (hasRequest)
    {
        const uint32_t until = millis() + LIDAR_TRIGGER_COOLDOWN_MS;
        portENTER_CRITICAL(&gTriggerDelayMux);
        if ((int32_t)(until - gBlockTriggersUntilMs) > 0)
        {
            gBlockTriggersUntilMs = until;
        }
        portEXIT_CRITICAL(&gTriggerDelayMux);
    }

    return hasRequest;
}

static void lidar_task(void *param)
{
    (void)param;

    uint32_t lastTriggerMs = 0;
    uint32_t lastDisplayMs = 0;
    uint16_t baselineCandidateMm = 0;
    uint32_t baselineSum = 0;
    uint16_t baselineSamples = 0;

    while (true)
    {
        uint32_t now = millis();

        if (!gLidarOk || !gEnabled || gPaused)
        {
            vTaskDelay(pdMS_TO_TICKS(LIDAR_POLL_INTERVAL_MS));
            continue;
        }

        uint16_t distanceMm = 0;
        bool ok = read_good_average(distanceMm);

        if (ok)
        {
            store_current_distance(distanceMm);

            if (now - lastDisplayMs >= 300)
            {
                lastDisplayMs = now;

                if (distanceMm == LIDAR_OUT_OF_RANGE_DISTANCE_MM)
                {
                    display_show_status("Distanza LiDAR:", "FUORI RANGE");
                }
                else
                {
                    display_show_status("Distanza LiDAR:", String(distanceMm));
                }
            }

            if (is_baseline_sample(distanceMm))
            {
                uint16_t baselineMm = 0;
                bool baselineReady = load_baseline(baselineMm);

                if (!baselineReady)
                {
                    if (baselineSamples == 0 ||
                        distance_delta(distanceMm, baselineCandidateMm) <= LIDAR_BASELINE_STABLE_DELTA_MM)
                    {
                        baselineCandidateMm = distanceMm;
                        baselineSum += distanceMm;
                        baselineSamples++;
                    }
                    else
                    {
                        baselineCandidateMm = distanceMm;
                        baselineSum = distanceMm;
                        baselineSamples = 1;
                    }

                    if (baselineSamples >= LIDAR_BASELINE_SAMPLE_COUNT)
                    {
                        baselineMm = (uint16_t)((baselineSum + (baselineSamples / 2)) / baselineSamples);
                        store_baseline(baselineMm);
                        baselineSamples = 0;
                        log_infof("lidar", "automatic baseline ready distance_mm=%u", baselineMm);
                    }
                }
                else
                {

                    const uint16_t thresholdMm = baseline_threshold_mm(baselineMm);
                    const uint16_t deltaMm = distance_delta(distanceMm, baselineMm);

                    if (is_trigger_distance(distanceMm) &&
                        distanceMm < thresholdMm &&
                        !triggers_are_blocked(now) &&
                        now - lastTriggerMs >= LIDAR_TRIGGER_COOLDOWN_MS)
                    {
                        bool queued = false;

                        portENTER_CRITICAL(&gRequestMux);
                        if (!gPhotoRequestPending)
                        {
                            gPhotoRequestDistanceMm = distanceMm;
                            gPhotoRequestPending = true;
                            queued = true;
                            lastTriggerMs = now;
                        }
                        portEXIT_CRITICAL(&gRequestMux);

                        if (queued)
                        {
                            log_infof("lidar", "photo request queued distance_mm=%u baseline_mm=%u threshold_mm=%u", distanceMm, baselineMm, thresholdMm);
                        }
                    }
                    else if (deltaMm <= LIDAR_BASELINE_STABLE_DELTA_MM)
                    {
                        track_baseline(distanceMm);
                    }
                }
            }
        }
        else
        {
            store_current_distance_invalid();

            if (now - lastDisplayMs >= 300)
            {
                lastDisplayMs = now;
                display_show_status("Distanza LiDAR:", "FUORI RANGE");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LIDAR_POLL_INTERVAL_MS));
    }
}

bool lidar_start_measure_task()
{
    if (!gLidarOk)
    {
        log_warn("lidar", "measurement task not started because sensor is not ready");
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        lidar_task,
        "lidar_task",
        LIDAR_TASK_STACK_BYTES,
        nullptr,
        LIDAR_TASK_PRIORITY,
        nullptr,
        LIDAR_TASK_CORE);

    if (ok != pdPASS)
    {
        log_error("lidar", "xTaskCreatePinnedToCore failed for measurement task");
        return false;
    }

    log_infof("lidar", "measure/trigger task started core=%d", LIDAR_TASK_CORE);
    return true;
}
