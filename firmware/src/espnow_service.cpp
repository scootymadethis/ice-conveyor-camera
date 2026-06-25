#include "espnow_service.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "app_config.h"
#include "app_log.h"

struct __attribute__((packed)) EspNowFrameChunk
{
    uint32_t magic;
    uint32_t frameId;
    uint32_t totalLen;
    uint16_t chunkIndex;
    uint16_t chunkCount;
    uint16_t payloadLen;
    uint8_t payload[ESPNOW_FRAME_CHUNK_BYTES];
};

static portMUX_TYPE s_lastFrameMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t *s_lastFrameBuf = nullptr;
static size_t s_lastFrameLen = 0;
static uint32_t s_lastFrameId = 0;
static const uint8_t s_espNowPeerMac[6] = ESP_NOW_PEER_MAC;
static const uint8_t s_broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static SemaphoreHandle_t s_sendDone = nullptr;
static volatile esp_now_send_status_t s_lastSendStatus = ESP_NOW_SEND_FAIL;
static volatile bool s_sendCallbackSeen = false;
static uint8_t s_senderWifiChannel = 0;

static void on_espnow_sent(const uint8_t *mac, esp_now_send_status_t status)
{
    (void)mac;
    s_lastSendStatus = status;
    s_sendCallbackSeen = true;
    if (s_sendDone)
    {
        xSemaphoreGive(s_sendDone);
    }
}


static bool peer_mac_is_configured()
{
    bool anyNonZero = false;
    bool allFf = true;

    for (int i = 0; i < 6; i++)
    {
        anyNonZero = anyNonZero || s_espNowPeerMac[i] != 0x00;
        allFf = allFf && s_espNowPeerMac[i] == 0xFF;
    }

    return anyNonZero && !allFf;
}

static void send_espnow_error(WiFiClient &client, const char *error)
{
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = error;

    String json;
    serializeJson(doc, json);

    client.print("ESPNOW_ERROR ");
    client.println(json);
}

static void send_espnow_ok(WiFiClient &client, size_t bytes, uint16_t chunkCount, uint32_t frameId)
{
    JsonDocument doc;
    doc["ok"] = true;
    doc["bytes"] = bytes;
    doc["chunk_count"] = chunkCount;
    doc["chunk_bytes"] = ESPNOW_FRAME_CHUNK_BYTES;
    doc["frame_id"] = frameId;

    String json;
    serializeJson(doc, json);

    client.print("ESPNOW_OK ");
    client.println(json);
}

static bool init_sender()
{
    if (!peer_mac_is_configured())
    {
        log_warn("espnow", "peer MAC is not configured; last-frame forwarding is disabled");
        return false;
    }

    if (!s_sendDone)
    {
        s_sendDone = xSemaphoreCreateBinary();
    }

    esp_err_t initResult = esp_now_init();
    if (initResult != ESP_OK && initResult != ESP_ERR_ESPNOW_EXIST)
    {
        log_errorf("espnow", "esp_now_init failed code=%d", initResult);
        return false;
    }

    esp_now_register_send_cb(on_espnow_sent);

    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78);

#if ESPNOW_FORCE_CHANNEL > 0
    const uint8_t wifiChannel = ESPNOW_FORCE_CHANNEL;
    if (WiFi.status() != WL_CONNECTED)
    {
        esp_wifi_set_promiscuous(true);
        esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);
        esp_wifi_set_promiscuous(false);
    }
#else
    const uint8_t wifiChannel = WiFi.channel();
#endif

    s_senderWifiChannel = wifiChannel;

    if (esp_now_is_peer_exist(s_espNowPeerMac))
    {
        esp_now_del_peer(s_espNowPeerMac);
    }
    if (esp_now_is_peer_exist(s_broadcastMac))
    {
        esp_now_del_peer(s_broadcastMac);
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, s_espNowPeerMac, 6);
    peerInfo.channel = wifiChannel;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = false;

    esp_err_t peerResult = esp_now_add_peer(&peerInfo);
    if (peerResult != ESP_OK && peerResult != ESP_ERR_ESPNOW_EXIST)
    {
        log_errorf("espnow", "esp_now_add_peer failed code=%d channel=%u", peerResult, wifiChannel);
        return false;
    }

    esp_now_peer_info_t broadcastPeer = {};
    memcpy(broadcastPeer.peer_addr, s_broadcastMac, 6);
    broadcastPeer.channel = wifiChannel;
    broadcastPeer.ifidx = WIFI_IF_STA;
    broadcastPeer.encrypt = false;
    esp_err_t broadcastResult = esp_now_add_peer(&broadcastPeer);
    if (broadcastResult != ESP_OK && broadcastResult != ESP_ERR_ESPNOW_EXIST)
    {
        log_warnf("espnow", "broadcast peer add failed code=%d channel=%u; continuing with unicast only", broadcastResult, wifiChannel);
    }

    log_infof("espnow", "sender ready v5 peer=%02X:%02X:%02X:%02X:%02X:%02X channel=%u chunk_bytes=%u",
              s_espNowPeerMac[0], s_espNowPeerMac[1], s_espNowPeerMac[2],
              s_espNowPeerMac[3], s_espNowPeerMac[4], s_espNowPeerMac[5], wifiChannel, ESPNOW_FRAME_CHUNK_BYTES);
    return true;
}

void espnow_store_last_frame(const uint8_t *data, size_t len)
{
    if (!data || len == 0)
    {
        return;
    }

    uint8_t *copy = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy)
    {
        copy = (uint8_t *)malloc(len);
    }

    if (!copy)
    {
        log_warnf("espnow", "could not cache last frame bytes=%u", (unsigned)len);
        return;
    }

    memcpy(copy, data, len);

    portENTER_CRITICAL(&s_lastFrameMux);
    uint8_t *old = s_lastFrameBuf;
    s_lastFrameBuf = copy;
    s_lastFrameLen = len;
    s_lastFrameId++;
    uint32_t frameId = s_lastFrameId;
    portEXIT_CRITICAL(&s_lastFrameMux);

    if (old)
    {
        free(old);
    }

    log_infof("espnow", "cached last frame frame_id=%u bytes=%u", (unsigned)frameId, (unsigned)len);
}

static void send_preflight_hello(uint32_t frameId, size_t len, uint16_t chunkCount, uint8_t wifiChannel)
{
    EspNowFrameChunk hello = {};
    hello.magic = ESPNOW_HELLO_MAGIC;
    hello.frameId = frameId;
    hello.totalLen = (uint32_t)len;
    hello.chunkIndex = wifiChannel; // For HELLO packets only: advertised sender Wi-Fi channel.
    hello.chunkCount = chunkCount;
    hello.payloadLen = 0;

    const uint32_t startMs = millis();
    uint16_t helloCount = 0;
    while (millis() - startMs < ESPNOW_PREFLIGHT_HELLO_MS)
    {
        esp_err_t result = esp_now_send(s_broadcastMac, (const uint8_t *)&hello, (sizeof(EspNowFrameChunk) - ESPNOW_FRAME_CHUNK_BYTES));
        (void)result;
        helloCount++;
        delay(ESPNOW_PREFLIGHT_HELLO_INTERVAL_MS);
    }

    log_infof("espnow", "preflight hello sent frame_id=%u channel=%u count=%u duration_ms=%u", (unsigned)frameId, wifiChannel, helloCount, ESPNOW_PREFLIGHT_HELLO_MS);
}

static bool send_chunk_with_ack(const EspNowFrameChunk &packet, uint32_t frameId, uint16_t chunkIndex, uint16_t chunkCount)
{
    for (uint8_t attempt = 1; attempt <= ESPNOW_SEND_RETRIES; attempt++)
    {
        if (s_sendDone)
        {
            while (xSemaphoreTake(s_sendDone, 0) == pdTRUE)
            {
            }
        }

        s_sendCallbackSeen = false;
        s_lastSendStatus = ESP_NOW_SEND_FAIL;

        esp_err_t result = esp_now_send(s_espNowPeerMac, (const uint8_t *)&packet, sizeof(packet));
        if (result != ESP_OK)
        {
            log_warnf("espnow", "chunk enqueue failed frame_id=%u chunk=%u/%u attempt=%u code=%d",
                      (unsigned)frameId, chunkIndex + 1, chunkCount, attempt, result);
            delay(20 * attempt);
            continue;
        }

        if (s_sendDone && xSemaphoreTake(s_sendDone, pdMS_TO_TICKS(ESPNOW_SEND_ACK_TIMEOUT_MS)) == pdTRUE)
        {
            if (s_sendCallbackSeen && s_lastSendStatus == ESP_NOW_SEND_SUCCESS)
            {
                return true;
            }

#if ESPNOW_REQUIRE_MAC_ACK
            log_warnf("espnow", "chunk not acked frame_id=%u chunk=%u/%u attempt=%u status=%d check detector MAC and SAME WiFi channel",
                      (unsigned)frameId, chunkIndex + 1, chunkCount, attempt, (int)s_lastSendStatus);
#else
            log_warnf("espnow", "chunk callback status=%d frame_id=%u chunk=%u/%u attempt=%u; continuing because ESPNOW_REQUIRE_MAC_ACK=0",
                      (int)s_lastSendStatus, (unsigned)frameId, chunkIndex + 1, chunkCount, attempt);
            return true;
#endif
        }
        else
        {
#if ESPNOW_REQUIRE_MAC_ACK
            log_warnf("espnow", "chunk ack timeout frame_id=%u chunk=%u/%u attempt=%u check detector channel",
                      (unsigned)frameId, chunkIndex + 1, chunkCount, attempt);
#else
            log_warnf("espnow", "chunk callback timeout frame_id=%u chunk=%u/%u attempt=%u; continuing because ESPNOW_REQUIRE_MAC_ACK=0",
                      (unsigned)frameId, chunkIndex + 1, chunkCount, attempt);
            return true;
#endif
        }

        delay(20 * attempt);
    }

    return false;
}

void handle_espnow_send_last(WiFiClient &client)
{
    if (!peer_mac_is_configured())
    {
        send_espnow_error(client, "ESP_NOW_PEER_MAC is not configured");
        return;
    }

    if (!init_sender())
    {
        send_espnow_error(client, "ESP-NOW init/peer failed");
        return;
    }

    portENTER_CRITICAL(&s_lastFrameMux);
    const uint8_t *frame = s_lastFrameBuf;
    const size_t len = s_lastFrameLen;
    const uint32_t frameId = s_lastFrameId;
    portEXIT_CRITICAL(&s_lastFrameMux);

    if (!frame || len == 0)
    {
        send_espnow_error(client, "No last frame cached yet");
        return;
    }

    const uint16_t chunkCount = (uint16_t)((len + ESPNOW_FRAME_CHUNK_BYTES - 1) / ESPNOW_FRAME_CHUNK_BYTES);
    EspNowFrameChunk packet = {};
    packet.magic = ESPNOW_FRAME_MAGIC;
    packet.frameId = frameId;
    packet.totalLen = (uint32_t)len;
    packet.chunkCount = chunkCount;

    log_infof("espnow", "sending cached frame frame_id=%u bytes=%u chunks=%u", (unsigned)frameId, (unsigned)len, chunkCount);
    send_preflight_hello(frameId, len, chunkCount, s_senderWifiChannel);

    for (uint16_t chunkIndex = 0; chunkIndex < chunkCount; chunkIndex++)
    {
        const size_t offset = (size_t)chunkIndex * ESPNOW_FRAME_CHUNK_BYTES;
        const size_t remaining = len - offset;
        const uint16_t payloadLen = (uint16_t)min((size_t)ESPNOW_FRAME_CHUNK_BYTES, remaining);

        packet.chunkIndex = chunkIndex;
        packet.payloadLen = payloadLen;
        memcpy(packet.payload, frame + offset, payloadLen);

        if (!send_chunk_with_ack(packet, frameId, chunkIndex, chunkCount))
        {
            log_errorf("espnow", "chunk send failed frame_id=%u chunk=%u/%u sender_channel=%u", (unsigned)frameId, chunkIndex + 1, chunkCount, s_senderWifiChannel);
            send_espnow_error(client, "ESP-NOW chunk send failed/timeout: receiver not ACKing. Check detector saw HELLO channel equal sender_channel, peer MAC is detector STA MAC, and both ESPs are close/powered.");
            return;
        }

        delay(ESPNOW_SEND_DELAY_MS);
    }

    log_infof("espnow", "cached frame sent frame_id=%u bytes=%u chunks=%u", (unsigned)frameId, (unsigned)len, chunkCount);
    send_espnow_ok(client, len, chunkCount, frameId);
}
