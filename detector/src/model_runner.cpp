#include "model_runner.h"

#include <math.h>
#include <stdlib.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"

#include "detection_decoder.h"
#include "detector_config.h"
#include "jpeg_decoder.h"
#include "logging.h"
#include "model_data.h"
#include "test_image.h"

alignas(16) static uint8_t s_tensorArena[MODEL_TENSOR_ARENA_BYTES];

static const tflite::Model *s_model = nullptr;
static tflite::MicroInterpreter *s_interpreter = nullptr;
static TfLiteTensor *s_input = nullptr;
static TfLiteTensor *s_output = nullptr;

static void quantize_rgb888_into_input(const uint8_t *src, int srcW, int srcH)
{
    const float scale = s_input->params.scale;
    const int zeroPoint = s_input->params.zero_point;
    int8_t *dst = s_input->data.int8;

    for (int y = 0; y < MODEL_INPUT_SIZE; y++)
    {
        const int sy = y * srcH / MODEL_INPUT_SIZE;
        for (int x = 0; x < MODEL_INPUT_SIZE; x++)
        {
            const int sx = x * srcW / MODEL_INPUT_SIZE;
            const uint8_t *pixel = src + (sy * srcW + sx) * IMG_C;
            int8_t *out = dst + (y * MODEL_INPUT_SIZE + x) * IMG_C;

            for (int channel = 0; channel < IMG_C; channel++)
            {
                float value = pixel[channel] / 255.0f;
                int quantized = lround(value / scale) + zeroPoint;
                if (quantized < -128)
                {
                    quantized = -128;
                }
                if (quantized > 127)
                {
                    quantized = 127;
                }
                out[channel] = (int8_t)quantized;
            }
        }
    }
}

static bool load_frame_into_input(const uint8_t *buffer, uint32_t length)
{
    const uint8_t *rgb = nullptr;
    int rgbW = 0;
    int rgbH = 0;
    uint8_t *decoded = nullptr;

    if (frame_is_jpeg(buffer, length))
    {
        decoded = jpeg_to_rgb888(buffer, length, &rgbW, &rgbH);
        if (!decoded)
        {
            return false;
        }
        rgb = decoded;
    }
    else
    {
        const uint32_t expected = (uint32_t)RAW_SRC_W * RAW_SRC_H * IMG_C;
        if (length != expected)
        {
            detector_log_warnf("model", "raw frame rejected bytes=%u expected=%u raw_shape=%dx%dx%d", (unsigned)length, (unsigned)expected, RAW_SRC_W, RAW_SRC_H, IMG_C);
            return false;
        }
        rgb = buffer;
        rgbW = RAW_SRC_W;
        rgbH = RAW_SRC_H;
    }

    quantize_rgb888_into_input(rgb, rgbW, rgbH);

    if (decoded)
    {
        free(decoded);
    }

    return true;
}

bool model_runner_begin()
{
#if MODEL_IS_PLACEHOLDER
    detector_log_error("model", "model_data.h is a placeholder; generate the real model before flashing");
    return false;
#endif

    tflite::InitializeTarget();

    s_model = tflite::GetModel(g_model);
    if (s_model->version() != TFLITE_SCHEMA_VERSION)
    {
        detector_log_error("model", "schema version mismatch; regenerate model_data.h");
        return false;
    }

    static tflite::MicroMutableOpResolver<8> resolver;
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddSoftmax();
    resolver.AddReshape();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddPad();
    resolver.AddMean();

    static tflite::ErrorReporter *errorReporter = tflite::GetMicroErrorReporter();

    static tflite::MicroInterpreter interpreter(
        s_model,
        resolver,
        s_tensorArena,
        MODEL_TENSOR_ARENA_BYTES,
        errorReporter,
        nullptr,
        nullptr);
    s_interpreter = &interpreter;

    if (s_interpreter->AllocateTensors() != kTfLiteOk)
    {
        detector_log_error("model", "AllocateTensors failed; increase MODEL_TENSOR_ARENA_BYTES");
        return false;
    }

    s_input = s_interpreter->input(0);
    s_output = s_interpreter->output(0);

    detector_log_infof("model", "arena used=%u / %u bytes", (unsigned)s_interpreter->arena_used_bytes(), (unsigned)MODEL_TENSOR_ARENA_BYTES);
    detector_log_infof("model", "input shape=%dx%dx%d scale=%.6f zero_point=%d",
                       s_input->dims->data[1], s_input->dims->data[2], s_input->dims->data[3],
                       s_input->params.scale, s_input->params.zero_point);
    return true;
}

bool model_runner_is_ready()
{
    return s_interpreter != nullptr && s_input != nullptr && s_output != nullptr;
}

void model_runner_run_frame(const uint8_t *buffer, uint32_t length)
{
    if (!model_runner_is_ready())
    {
        detector_log_error("model", "interpreter not ready; frame skipped");
        return;
    }

    if (!load_frame_into_input(buffer, length))
    {
        return;
    }

    uint32_t startedUs = micros();
    if (s_interpreter->Invoke() != kTfLiteOk)
    {
        detector_log_error("model", "Invoke failed");
        return;
    }

    uint32_t elapsedUs = micros() - startedUs;
    detector_log_infof("model", "inference complete elapsed_ms=%.1f", elapsedUs / 1000.0f);
    print_detections(s_output);
}

void model_runner_run_self_test()
{
    if (!model_runner_is_ready())
    {
        return;
    }

    detector_log_info("model", "running embedded self-test image");
    quantize_rgb888_into_input(g_test_image, IMG_W, IMG_H);

    if (s_interpreter->Invoke() == kTfLiteOk)
    {
        print_detections(s_output);
    }
    else
    {
        detector_log_error("model", "self-test Invoke failed");
    }
}
