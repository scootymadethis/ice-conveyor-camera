#pragma once

// LiDAR I2C pins.
//
// IMPORTANT: do not use GPIO17/GPIO18 on ESP32-S3-EYE while the camera is
// enabled: those pins are camera data lines (Y8/Y7). They work in the standalone
// test-sensore sketch because the camera is not initialised, then fail in the
// full firmware once esp_camera_init() takes ownership of the pins.
//
// Wire the VL53L0X as follows:
//   VL53L0X VIN  -> 3V3
//   VL53L0X GND  -> GND
//   VL53L0X SDA  -> GPIO1
//   VL53L0X SCL  -> GPIO2
//   VL53L0X XSHUT -> 3V3 (or leave tied high on breakout boards that do this)
#ifndef I2C_SDA
#define I2C_SDA 1
#endif

#ifndef I2C_SCL
#define I2C_SCL 2
#endif
