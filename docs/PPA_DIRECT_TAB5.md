# PPA DIRECT on M5Stack Tab5 (ESP32-P4)

**Verified on Tab5 hardware: 2026-11-22.** Source: [`src/m5stack_main.cpp`](../src/m5stack_main.cpp).

R8T5 renders its 128 × 128 RGB565 emulated screen to the Tab5 display using ESP32-P4 PPA scale/rotate/mirror (SRM) directly into the live DSI framebuffer, bypassing a CPU transpose/scale staging step. The maintainer observed approximately **2× faster rendering than the previous CPU path** in their use case; this is not a controlled cross-project benchmark.

## Display ownership and framebuffer

R8T5 uses **M5Unified / LovyanGFX `Panel_DSI`**, not an independently allocated `esp_lcd_new_panel_dpi()` framebuffer. After `M5.begin(cfg)`:

```cpp
auto *panel_dev = M5.Display.getPanel();
auto *dsi = static_cast<lgfx::Panel_DSI *>(panel_dev);
const auto detail = dsi->config_detail();
const auto cfg = dsi->config();
uint16_t *dsi_fb = static_cast<uint16_t *>(detail.buffer);
const int physical_w = cfg.panel_width;   // 720 on tested Tab5
const int physical_h = cfg.panel_height; // 1280 on tested Tab5
const uint32_t bytes = (uint32_t)physical_w * physical_h * sizeof(uint16_t);
const bool direct_ok = dsi_fb && ((uintptr_t)dsi_fb % 128) == 0 && (bytes % 128) == 0;
```

The physical buffer is **720 × 1280 RGB565 = 1,843,200 bytes** (a landscape 1280 × 720 view rotated in memory). The code requires both the pointer and the declared buffer byte length to be 128-byte aligned before allowing PPA DIRECT. The working device passed these checks; the absolute pointer address depends on allocation and is not hardcoded.

## PPA client configuration

```cpp
ppa_client_config_t client_cfg = {};
client_cfg.oper_type = PPA_OPERATION_SRM;
client_cfg.max_pending_trans_num = 1;
client_cfg.data_burst_length = PPA_DATA_BURST_LENGTH_128;
ESP_ERROR_CHECK(ppa_register_client(&client_cfg, &s_ppa_srm));
```

R8T5 uses the returned client handle with **one blocking SRM operation per frame**. The actual application checks for errors and falls back rather than aborting.

**Burst default:** Espressif's `ppa_core.c` sets `client->data_burst_length` to `PPA_DATA_BURST_LENGTH_128` when the config field is zero. Thus the explicit `data_burst_length = PPA_DATA_BURST_LENGTH_128` above has no extra tuning effect and is optional.

## SRM operation

Below is the essential configuration for the tested **128 × 128 to centered 720 × 720** image. The framebuffer layout and offsets are specific to Tab5's panel rotation.

```cpp
ppa_srm_oper_config_t op = {};
op.in.buffer = source_rgb565;
op.in.pic_w = 128;
op.in.pic_h = 128;
op.in.block_w = 128;
op.in.block_h = 128;
op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

op.out.buffer = dsi_fb;
op.out.buffer_size = bytes;   // Important: explicit byte length, 128-aligned
op.out.pic_w = physical_w;    // 720
op.out.pic_h = physical_h;   // 1280
op.out.block_offset_x = 0;
op.out.block_offset_y = 280; // (1280 - 720) / 2 for this centered image
op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

op.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
op.scale_x = 720.0f / 128.0f;
op.scale_y = 720.0f / 128.0f;
op.mode = PPA_TRANS_MODE_BLOCKING;

M5.Display.waitDMA();
esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_srm, &op);
```

The production source computes the physical offsets from the logical display geometry rather than fixing them unconditionally. The source buffer only needs to hold the 128 × 128 RGB565 input region; the output writes directly into the DSI framebuffer.

## `ESP_ERR_INVALID_ARG` / `0x102` checklist

Espressif's ESP-IDF v5.5.4 implementation explicitly rejects an output pointer that is outside internal RAM/PSRAM, or if **either `op.out.buffer` or `op.out.buffer_size` is not aligned to the driver's required buffer alignment**. It also validates color modes, rectangle dimensions and offsets.

Check the following before experimenting with other alignments or panels:

1. Set **`op.out.buffer_size`**, measured in bytes; leaving it zero/omitted does not satisfy the alignment check alone and may fail later size checks.
2. Verify that `op.out.buffer` is memory accessible to PPA (RAM/PSRAM) and sufficiently aligned. In this tested configuration it was 128-byte aligned.
3. Set **both** `op.in.srm_cm` and `op.out.srm_cm` to `PPA_SRM_COLOR_MODE_RGB565`.
4. Set physical `op.out.pic_w/pic_h`, not logical landscape dimensions, and ensure the scaled 270-degree region and offsets fit.
5. Ensure `ppa_register_client()` succeeds and `op.mode = PPA_TRANS_MODE_BLOCKING`.

Driver validation: [`esp-idf` v5.5.4 `ppa_srm.c`, `ppa_do_scale_rotate_mirror()`](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_ppa/src/ppa_srm.c#L187-L240). The alignment check precedes the *declared output picture length* check. In [Issue #1's verified follow-up](https://github.com/Layer812/R8T5/issues/1#issuecomment-7135872435), the reporter confirmed that their `0x102` was caused by omitting `op.out.buffer_size`. This diagnosis was confirmed for their setup, not for all `0x102` errors.

## Pixel-art fidelity: interpolating scale is not nearest-neighbour

**Important limitation of the v0.65 direct renderer:** PPA SRM with fractional scale (here 5.625x) **interpolates pixels**, so it can blend edges and produce colors not present in the original PICO-8 image. In [the independent retro-go test report](https://github.com/Layer812/R8T5/issues/1#issuecomment-7135872435), 104 of 144 sample pixels differed from nearest-neighbour; the reporter obtained byte-identical output using **CPU nearest-neighbour upscale + PPA at 1:1 to rotate/transfer**. Their reported per-block performance applies to their workload/hardware, **not automatically to R8T5**.

A [separate experimental R8T5 implementation](https://github.com/Layer812/R8T5/blob/work/r8t5-ppa-crisp-dirty-retry-20261128/docs/PPA_CRISP_EXPERIMENT.md) (development branch, **not public v0.65**) uses dirty 32x32 source tiles, CPU nearest-neighbour to 180x180 and PPA 1:1 rotate into the live DSI buffer. Hardware behavior and speed are pending actual Tab5 testing.

## Runtime behavior and fallback

The published v0.65 present task calls `M5.Display.waitDMA()`, then attempts direct SRM. On the first direct failure it disables direct mode for the remainder of the session, falling back to staged PPA/CPU as available. The issue reporter also observed long transient latency spikes under memory contention; **one failure may not be a permanent configuration defect**. An automatic retry/cooldown experiment is included in the separate development branch; the published v0.65 has not been modified.

Typical log on a successful direct frame:

```text
[R8T5] renderer: direct
```

See [`init_ppa_fast_present()`, `ppa_scale_rgb565()`, and `ppa_present_region_scaled()`](../src/m5stack_main.cpp), plus the `flipTask()` call site, for the complete hardware-specific implementation.

Related question: [Issue #1](https://github.com/Layer812/R8T5/issues/1).
