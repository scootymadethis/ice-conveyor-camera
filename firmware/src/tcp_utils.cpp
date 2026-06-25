#include "tcp_utils.h"

#include "app_config.h"
#include "app_log.h"

bool read_client_line(WiFiClient &client, String &out, unsigned long timeout_ms)
{
    out = "";
    unsigned long start = millis();

    while (millis() - start < timeout_ms)
    {
        if (!client || !client.connected())
        {
            return false;
        }

        while (client.available() > 0)
        {
            char c = (char)client.read();

            if (c == '\n')
            {
                out.trim();
                return true;
            }

            if (c != '\r')
            {
                out += c;
            }

            if (out.length() > 128)
            {
                out.trim();
                log_warn("tcp", "client line exceeded 128 chars; returning truncated command line");
                return true;
            }
        }

        delay(1);
    }

    out.trim();
    return false;
}

bool send_all(
    WiFiClient &client,
    const uint8_t *data,
    size_t len,
    unsigned long stall_timeout_ms,
    unsigned long total_timeout_ms)
{
    size_t total_sent = 0;
    unsigned long total_start = millis();
    unsigned long last_progress = millis();

    while (total_sent < len)
    {
        if (!client || !client.connected())
        {
            log_warnf("tcp", "client disconnected while sending bytes_sent=%u bytes_total=%u", (unsigned)total_sent, (unsigned)len);
            return false;
        }

        unsigned long now = millis();

        if (now - total_start > total_timeout_ms)
        {
            log_warnf("tcp", "total send timeout bytes_sent=%u bytes_total=%u timeout_ms=%lu", (unsigned)total_sent, (unsigned)len, total_timeout_ms);
            return false;
        }

        if (now - last_progress > stall_timeout_ms)
        {
            log_warnf("tcp", "send stalled bytes_sent=%u bytes_total=%u stall_timeout_ms=%lu", (unsigned)total_sent, (unsigned)len, stall_timeout_ms);
            return false;
        }

        size_t remaining = len - total_sent;
        size_t chunk_len = remaining > TCP_SEND_CHUNK_BYTES ? TCP_SEND_CHUNK_BYTES : remaining;
        size_t sent = client.write(data + total_sent, chunk_len);

        if (sent > 0)
        {
            total_sent += sent;
            last_progress = millis();
            delay(0);
        }
        else
        {
            delay(1);
        }
    }

    return true;
}

bool send_line(WiFiClient &client, const String &line, unsigned long timeout_ms)
{
    String payload = line + "\n";
    return send_all(client, (const uint8_t *)payload.c_str(), payload.length(), timeout_ms, timeout_ms);
}

bool send_line(WiFiClient &client, const char *line, unsigned long timeout_ms)
{
    String payload = String(line ? line : "") + "\n";
    return send_all(client, (const uint8_t *)payload.c_str(), payload.length(), timeout_ms, timeout_ms);
}

bool send_line_default(WiFiClient &client, const String &line)
{
    return send_line(client, line, TCP_SEND_STALL_TIMEOUT_MS);
}

bool send_line_default(WiFiClient &client, const char *line)
{
    return send_line(client, line, TCP_SEND_STALL_TIMEOUT_MS);
}
