#include "espnow_receiver.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "detector_config.h"
#include "espnow_frame.h"
#include "logging.h"

static uint8_t *s_imageBuffer = nullptr;
static uint32_t s_currentFrameId = 0;
static uint16_t s_chunksReceived = 0;
static uint16_t s_expectedChunkCount = 0;
static uint32_t s_expectedTotalLen = 0;
static bool *s_receivedChunks = nullptr;
static bool s_frameActive = false;
static volatile bool s_frameReady = false;
static volatile uint32_t s_frameLen = 0;
static volatile bool s_channelLocked = false;
static TaskHandle_t s_channelHopTask = nullptr;
static volatile uint8_t s_lockedChannel = 0;


static bool allocate_frame_storage()
{
    if (!s_imageBuffer)
    {
        s_imageBuffer = (uint8_t *)heap_caps_malloc(MAX_IMAGE_SIZE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_imageBuffer)
        {
            detector_log_warnf("espnow", "PSRAM image buffer allocation failed bytes=%u; trying internal heap", (unsigned)MAX_IMAGE_SIZE_BYTES);
            s_imageBuffer = (uint8_t *)heap_caps_malloc(MAX_IMAGE_SIZE_BYTES, MALLOC_CAP_8BIT);
        }
        if (!s_imageBuffer)
        {
            detector_log_errorf("espnow", "image buffer allocation failed bytes=%u; lower MAX_IMAGE_SIZE_BYTES or enable PSRAM", (unsigned)MAX_IMAGE_SIZE_BYTES);
            return false;
        }
    }

    if (!s_receivedChunks)
    {
        size_t bitmapBytes = MAX_ESPNOW_FRAME_CHUNKS * sizeof(bool);
        s_receivedChunks = (bool *)heap_caps_malloc(bitmapBytes, MALLOC_CAP_8BIT);
        if (!s_receivedChunks)
        {
            detector_log_errorf("espnow", "chunk bitmap allocation failed bytes=%u", (unsigned)bitmapBytes);
            return false;
        }
        memset(s_receivedChunks, 0, bitmapBytes);
    }

    detector_log_infof(
        "espnow",
        "frame storage ready max_image=%u chunk_bitmap=%u free_heap=%u free_psram=%u",
        (unsigned)MAX_IMAGE_SIZE_BYTES,
        (unsigned)(MAX_ESPNOW_FRAME_CHUNKS * sizeof(bool)),
        (unsigned)ESP.getFreeHeap(),
        (unsigned)ESP.getFreePsram());
    return true;
}

static size_t frame_header_size()
{
    return sizeof(EspNowFrameChunk) - ESPNOW_FRAME_CHUNK_BYTES;
}

static bool chunk_has_valid_shape(const EspNowFrameChunk &chunk, int len)
{
    if ((size_t)len < frame_header_size())
    {
        detector_log_warnf("espnow", "packet too short bytes=%d min_header=%u", len, (unsigned)frame_header_size());
        return false;
    }

    if (chunk.magic != ESPNOW_FRAME_MAGIC)
    {
        detector_log_warnf("espnow", "packet ignored because magic mismatch got=0x%08X", (unsigned)chunk.magic);
        return false;
    }

    if (chunk.totalLen == 0 || chunk.totalLen > MAX_IMAGE_SIZE_BYTES)
    {
        detector_log_warnf("espnow", "frame rejected invalid total_len=%u max=%u", (unsigned)chunk.totalLen, (unsigned)MAX_IMAGE_SIZE_BYTES);
        return false;
    }

    if (chunk.chunkCount == 0 || chunk.chunkCount > MAX_ESPNOW_FRAME_CHUNKS || chunk.chunkIndex >= chunk.chunkCount)
    {
        detector_log_warnf("espnow", "chunk rejected index=%u count=%u max=%u", chunk.chunkIndex, chunk.chunkCount, (unsigned)MAX_ESPNOW_FRAME_CHUNKS);
        return false;
    }

    if (chunk.payloadLen > ESPNOW_FRAME_CHUNK_BYTES)
    {
        detector_log_warnf("espnow", "chunk rejected payload_len=%u max=%u", chunk.payloadLen, ESPNOW_FRAME_CHUNK_BYTES);
        return false;
    }

    if (frame_header_size() + chunk.payloadLen > (size_t)len)
    {
        detector_log_warnf("espnow", "chunk rejected payload_len=%u packet_bytes=%d", chunk.payloadLen, len);
        return false;
    }

    uint32_t offset = (uint32_t)chunk.chunkIndex * ESPNOW_FRAME_CHUNK_BYTES;
    if (offset + chunk.payloadLen > MAX_IMAGE_SIZE_BYTES || offset + chunk.payloadLen > chunk.totalLen)
    {
        detector_log_warnf("espnow", "chunk rejected offset=%u payload=%u total=%u", (unsigned)offset, chunk.payloadLen, (unsigned)chunk.totalLen);
        return false;
    }

    return true;
}

static void reset_for_new_frame(const EspNowFrameChunk &chunk)
{
    s_currentFrameId = chunk.frameId;
    s_chunksReceived = 0;
    s_expectedChunkCount = chunk.chunkCount;
    s_expectedTotalLen = chunk.totalLen;
    memset(s_receivedChunks, 0, MAX_ESPNOW_FRAME_CHUNKS * sizeof(bool));
    s_frameActive = true;
    detector_log_infof("espnow", "receiving frame frame_id=%u bytes=%u chunks=%u", (unsigned)chunk.frameId, (unsigned)chunk.totalLen, chunk.chunkCount);
}

static uint8_t current_wifi_channel()
{
    uint8_t primaryChannel = 0;
    wifi_second_chan_t secondChannel = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primaryChannel, &secondChannel);
    return primaryChannel;
}

static bool set_wifi_channel(uint8_t channel)
{
    if (channel < 1 || channel > 13)
    {
        return false;
    }

    esp_wifi_set_promiscuous(true);
    esp_err_t result = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    return result == ESP_OK;
}

static void lock_channel_from_sender_hello(const uint8_t *mac, const EspNowFrameChunk &hello)
{
    const uint8_t advertisedChannel = (uint8_t)hello.chunkIndex; // For HELLO only: sender Wi-Fi channel.
    const uint8_t beforeChannel = current_wifi_channel();

    if (advertisedChannel < 1 || advertisedChannel > 13)
    {
        detector_log_warnf("espnow", "preflight hello ignored invalid advertised_channel=%u current_channel=%u", advertisedChannel, beforeChannel);
        return;
    }

    if (!s_channelLocked || s_lockedChannel != advertisedChannel)
    {
        s_channelLocked = true;
        s_lockedChannel = advertisedChannel;
        bool channelSet = set_wifi_channel(advertisedChannel);
        detector_log_infof(
            "espnow",
            "preflight hello locked advertised_channel=%u previous_channel=%u actual_channel=%u set_ok=%u sender=%02X:%02X:%02X:%02X:%02X:%02X frame_id=%u bytes=%u chunks=%u",
            advertisedChannel,
            beforeChannel,
            current_wifi_channel(),
            channelSet ? 1 : 0,
            mac ? mac[0] : 0, mac ? mac[1] : 0, mac ? mac[2] : 0,
            mac ? mac[3] : 0, mac ? mac[4] : 0, mac ? mac[5] : 0,
            (unsigned)hello.frameId, (unsigned)hello.totalLen, hello.chunkCount);
    }
}

static void channel_hop_task(void *param)
{
    (void)param;
    uint8_t channel = 1;

    while (true)
    {
        if (!s_channelLocked && !s_frameActive && !s_frameReady)
        {
            set_wifi_channel(channel);
            channel = (channel >= 13) ? 1 : channel + 1;
        }

        vTaskDelay(pdMS_TO_TICKS(ESPNOW_CHANNEL_HOP_INTERVAL_MS));
    }
}

static void on_data_recv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    // While loop() is processing the previous frame, drop new chunks instead of
    // overwriting the shared image buffer.
    if (s_frameReady)
    {
        return;
    }

    EspNowFrameChunk chunk = {};
    size_t copyLen = len < (int)sizeof(chunk) ? (size_t)len : sizeof(chunk);
    memcpy(&chunk, incomingData, copyLen);

    if (chunk.magic == ESPNOW_HELLO_MAGIC)
    {
        if ((size_t)len >= frame_header_size())
        {
            lock_channel_from_sender_hello(mac, chunk);
        }
        return;
    }

    if (!chunk_has_valid_shape(chunk, len))
    {
        s_frameActive = false;
        return;
    }

    s_channelLocked = true;
    s_lockedChannel = current_wifi_channel();

    if (!s_frameActive || chunk.frameId != s_currentFrameId)
    {
        reset_for_new_frame(chunk);
    }

    if (chunk.chunkCount != s_expectedChunkCount || chunk.totalLen != s_expectedTotalLen)
    {
        detector_log_warnf("espnow", "frame metadata changed frame_id=%u", (unsigned)chunk.frameId);
        reset_for_new_frame(chunk);
    }

    uint32_t offset = (uint32_t)chunk.chunkIndex * ESPNOW_FRAME_CHUNK_BYTES;
    memcpy(s_imageBuffer + offset, chunk.payload, chunk.payloadLen);
    if (!s_receivedChunks[chunk.chunkIndex])
    {
        s_receivedChunks[chunk.chunkIndex] = true;
        s_chunksReceived++;
    }

    if (s_chunksReceived >= s_expectedChunkCount)
    {
        s_frameActive = false;
        s_frameLen = chunk.totalLen;
        s_frameReady = true;
        detector_log_infof("espnow", "frame complete frame_id=%u bytes=%u chunks=%u", (unsigned)chunk.frameId, (unsigned)chunk.totalLen, chunk.chunkCount);
    }
}

bool espnow_receiver_begin()
{
    if (!allocate_frame_storage())
    {
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78);

#if ESPNOW_WIFI_CHANNEL > 0
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
#endif

    uint8_t primaryChannel = 0;
    wifi_second_chan_t secondChannel = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primaryChannel, &secondChannel);
    detector_log_infof("espnow", "detector STA MAC=%s configured_channel=%u actual_channel=%u chunk_bytes=%u", WiFi.macAddress().c_str(), ESPNOW_WIFI_CHANNEL, primaryChannel, ESPNOW_FRAME_CHUNK_BYTES);

#if ESPNOW_WIFI_CHANNEL > 0
    s_channelLocked = true;
    s_lockedChannel = primaryChannel;
#else
    detector_log_info("espnow", "auto channel hop enabled; waiting for sender preflight hello");
#endif

    esp_err_t result = esp_now_init();
    if (result != ESP_OK)
    {
        detector_log_errorf("espnow", "esp_now_init failed code=%d", result);
        return false;
    }

    esp_now_register_recv_cb(on_data_recv);

#if ESPNOW_WIFI_CHANNEL == 0
    if (!s_channelHopTask)
    {
        xTaskCreatePinnedToCore(
            channel_hop_task,
            "espnow_channel_hop",
            2048,
            nullptr,
            1,
            &s_channelHopTask,
            0);
    }
#endif

    detector_log_info("espnow", "receiver ready; waiting for frame chunks");
    return true;
}

bool espnow_frame_available()
{
    return s_frameReady;
}

ReceivedFrame espnow_current_frame()
{
    return {s_imageBuffer, s_frameLen, s_currentFrameId};
}

void espnow_release_frame()
{
    s_frameReady = false;
    s_frameLen = 0;
}
