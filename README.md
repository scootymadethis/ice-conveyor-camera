# Patch v4 - ESP-NOW detector large frame buffer in PSRAM

This patch fixes the detector build error caused by raising `MAX_IMAGE_SIZE_BYTES` to large values such as 200 KB:

```text
.dram0.bss will not fit in region dram0_0_seg
DRAM segment data does not fit
```

## What changed

- `detector/src/espnow_receiver.cpp`
  - Replaced the static global image buffer:
    ```cpp
    static uint8_t s_imageBuffer[MAX_IMAGE_SIZE_BYTES];
    ```
    with a dynamically allocated buffer.
  - The frame buffer is allocated at boot with:
    ```cpp
    heap_caps_malloc(MAX_IMAGE_SIZE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    ```
  - If PSRAM allocation fails, it tries internal heap and then prints a clear error.
  - The received-chunk bitmap is also dynamically allocated.
  - Boot log now prints max frame size, bitmap size, free heap, and free PSRAM.

- `detector/include/detector_config.h`
  - Raised detector receive limit to:
    ```cpp
    #define MAX_IMAGE_SIZE_BYTES (220 * 1024)
    ```
  - This accepts your 141 KB JPEG with margin.

## Important

This fixes the compile/linker problem. It does not make ESP-NOW itself faster. A 141 KB JPEG means roughly 800 ESP-NOW chunks at 180 bytes each, so transfer will be slow and more fragile than a 40-60 KB frame.

For reliable production, prefer lowering the sender JPEG size with smaller frame size or higher JPEG quality number. Use the 220 KB receiver limit as a safety net, not as the normal target size.

## Expected detector boot log

After flashing detector, you should see something like:

```text
[INFO][espnow] frame storage ready max_image=225280 chunk_bitmap=1252 free_heap=... free_psram=...
[INFO][espnow] detector STA MAC=... configured_channel=0 actual_channel=... chunk_bytes=180
[INFO][espnow] auto channel hop enabled; waiting for sender preflight hello
[INFO][espnow] receiver ready; waiting for frame chunks
```

If `free_psram=0`, PSRAM is not enabled or not detected. Check PlatformIO uses:

```ini
board_build.arduino.memory_type = qio_opi
build_flags =
    -DBOARD_HAS_PSRAM
```
