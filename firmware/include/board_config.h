#pragma once
// -----------------------------------------------------------------------------
// Board-level, non-secret configuration.
// -----------------------------------------------------------------------------

// The camera model is selected via build_flags (-DCAMERA_MODEL_ESP32S3_EYE).
#if !defined(CAMERA_MODEL_ESP32S3_EYE)
#warning "CAMERA_MODEL_ESP32S3_EYE is not defined; set it in platformio.ini build_flags"
#endif


