#include "detection_decoder.h"

#include <Arduino.h>

#include "detector_config.h"
#include "logging.h"

static const char *CLASS_NAMES[MODEL_CLASS_COUNT] = {"background", "Lego", "Cover"};

void print_detections(const TfLiteTensor *output)
{
    const float outputScale = output->params.scale;
    const int outputZeroPoint = output->params.zero_point;
    const int8_t *data = output->data.int8;

    int found = 0;
    for (int gy = 0; gy < MODEL_GRID_SIZE; gy++)
    {
        for (int gx = 0; gx < MODEL_GRID_SIZE; gx++)
        {
            const int base = (gy * MODEL_GRID_SIZE + gx) * MODEL_CLASS_COUNT;
            int bestClass = 0;
            float bestProbability = -1.0f;

            for (int classIndex = 0; classIndex < MODEL_CLASS_COUNT; classIndex++)
            {
                float probability = (data[base + classIndex] - outputZeroPoint) * outputScale;
                if (probability > bestProbability)
                {
                    bestProbability = probability;
                    bestClass = classIndex;
                }
            }

            if (bestClass != 0 && bestProbability >= MODEL_CONFIDENCE_THRESHOLD)
            {
                int px = (int)((gx + 0.5f) * MODEL_INPUT_SIZE / MODEL_GRID_SIZE);
                int py = (int)((gy + 0.5f) * MODEL_INPUT_SIZE / MODEL_GRID_SIZE);
                detector_log_infof("detect", "class=%s probability=%.2f cell=(%d,%d) px=(%d,%d)", CLASS_NAMES[bestClass], bestProbability, gx, gy, px, py);
                found++;
            }
        }
    }

    if (!found)
    {
        detector_log_info("detect", "no objects above confidence threshold");
    }
}
