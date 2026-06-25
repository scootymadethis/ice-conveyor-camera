#include "jpeg_decoder.h"

#include <TJpg_Decoder.h>
#include <stdlib.h>

#include "detector_config.h"
#include "logging.h"

static uint8_t *s_jpegRgb = nullptr;
static int s_jpegW = 0;
static int s_jpegH = 0;

bool frame_is_jpeg(const uint8_t *buffer, uint32_t length)
{
    return length >= 3 && buffer[0] == 0xFF && buffer[1] == 0xD8 && buffer[2] == 0xFF;
}

static bool tjpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
    if (!s_jpegRgb)
    {
        return false;
    }

    for (uint16_t by = 0; by < h; by++)
    {
        const int dy = y + by;
        if (dy < 0 || dy >= s_jpegH)
        {
            continue;
        }

        for (uint16_t bx = 0; bx < w; bx++)
        {
            const int dx = x + bx;
            if (dx < 0 || dx >= s_jpegW)
            {
                continue;
            }

            const uint16_t color = bitmap[by * w + bx];
            uint8_t *out = s_jpegRgb + ((size_t)dy * s_jpegW + dx) * 3;
            out[0] = ((color >> 11) & 0x1F) << 3;
            out[1] = ((color >> 5) & 0x3F) << 2;
            out[2] = (color & 0x1F) << 3;
        }
    }

    return true;
}

static uint8_t choose_scale(uint16_t width, uint16_t height)
{
    uint8_t scale = 1;
    while (scale < 8 &&
           (width / (scale * 2)) >= MODEL_INPUT_SIZE &&
           (height / (scale * 2)) >= MODEL_INPUT_SIZE)
    {
        scale *= 2;
    }
    return scale;
}

uint8_t *jpeg_to_rgb888(const uint8_t *jpeg, uint32_t length, int *out_w, int *out_h)
{
    uint16_t width = 0;
    uint16_t height = 0;

    if (TJpgDec.getJpgSize(&width, &height, jpeg, length) != JDR_OK || width == 0 || height == 0)
    {
        detector_log_warn("jpeg", "header unreadable; frame skipped");
        return nullptr;
    }

    uint8_t scale = choose_scale(width, height);
    int rgbW = width / scale;
    int rgbH = height / scale;
    size_t rgbBytes = (size_t)rgbW * rgbH * 3;

    uint8_t *out = (uint8_t *)malloc(rgbBytes);
    if (!out)
    {
        detector_log_warnf("jpeg", "not enough RAM for RGB888 buffer width=%d height=%d bytes=%u", rgbW, rgbH, (unsigned)rgbBytes);
        return nullptr;
    }

    s_jpegRgb = out;
    s_jpegW = rgbW;
    s_jpegH = rgbH;

    TJpgDec.setJpgScale(scale);
    TJpgDec.setCallback(tjpg_output);
    JRESULT result = TJpgDec.drawJpg(0, 0, jpeg, length);
    s_jpegRgb = nullptr;

    if (result != JDR_OK)
    {
        detector_log_warnf("jpeg", "decode failed code=%d", result);
        free(out);
        return nullptr;
    }

    detector_log_infof("jpeg", "decoded %ux%u -> RGB888 %dx%d scale=1/%u", width, height, rgbW, rgbH, scale);
    *out_w = rgbW;
    *out_h = rgbH;
    return out;
}
