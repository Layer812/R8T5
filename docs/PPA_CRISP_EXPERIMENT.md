# R8T5 experimental crisp PPA (ESP32-P4 Tab5)

Status: **development branch; not yet validated on Tab5 hardware**. Keep published v0.65 and its M5Burner share code unchanged until physical tests are complete.

See [Issue #1](https://github.com/Layer812/R8T5/issues/1#issuecomment-7135872435), where the retro-go maintainer confirmed that missing `op.out.buffer_size` caused their `0x102` and independently documented interpolation in fractional PPA scaling and an alternative 1:1 rotate path.

## Why change

The previously verified direct path did one `128x128 -> 720x720` fractional scale+270° rotate SRM operation and was approximately twice as fast in its original test. However **the PPA scaling interpolates pixel colors** and is not nearest-neighbour. The reporter's comparison was **104/144 sampled pixels different** from CPU nearest-neighbour when scaling, showing weighted neighboring colors. This does not invalidate the reported speedup; it identifies a pixel-art fidelity trade-off.

## New test-mode pipeline

1. Keep one RGB565 720x720 PSRAM staging buffer (already allocated by the legacy fallback).
2. Divide the 128x128 source into sixteen **32x32** tiles, mapping exactly to sixteen **180x180** pixel tiles of the centered 720x720 landscape viewport.
3. For a dirty tile, CPU-expand its pixels via global `src_idx[p] = floor(p * 128 / 720)`. Only that output region is touched; unmodified tiles retain the previous expanded pixels in the persistent buffer.
4. Call blocking PPA SRM for the **180x180 rectangle** in the staging buffer, into the live aligned DSI framebuffer. Set **`scale_x = scale_y = 1.0`**, **270° rotation** and **`op.out.buffer_size` = actual physical RGB565 framebuffer bytes**. Input `pic_w = pic_h = 720`; input `block_offset_x/y = 180 * tile_x/y`; output physical offsets account for the panel's rotation.
5. On first frame, after GUI redraw or when an operation fails, treat the next frame as fully dirty.
6. If PPA fails after one or more tiles, immediately expand a **complete** nearest-neighbour 720x720 frame and use the proven CPU + M5.Display.pushImageDMA fallback. This repairs any partial tile output. A transient PPA failure triggers cooldown and automatic retry; a configuration error `ESP_ERR_INVALID_ARG` disables the new experimental path for the session.
7. Log `[R8T5] renderer: crisp-1to1` for the new path. Periodically log dirty tiles (of 16) and `present_us`; this is software pipeline time, **not standalone DMA time**. Errors and successful recovery are also logged.

## Build comparison (experimental branch only)

`src/m5stack_main.cpp` defines `R8T5_EXPERIMENTAL_CRISP_PPA=1` by default for this experimental branch. Use build flag `-D R8T5_EXPERIMENTAL_CRISP_PPA=0` to compile the original interpolating PPA path for a same-cart comparison. The v0.65 production main branch is untouched.

If the 720x720 staging buffer cannot be allocated, the emergency last-resort PPA direct path may still use interpolation. The log calls this renderer `direct`, not `crisp-1to1`.

## Host regression result (not hardware validation)

A separate C++ reference model compares the new tile dirty-detection geometry and logical-to-physical 270° rotation against a full nearest-neighbour reference (720x720 centered in physical 720x1280). Tested initial frame, unchanged frame, single-pixel changes at opposite corners, tile boundary changes, forced UI invalidation and full recompose after a simulated PPA failure: **0 different pixels** in all 7 cases. This validates the algorithm **only**, not actual PPA pixel mapping, ESP-IDF DMA hardware timing, visual appearance or hardware compilation.

## Actual Tab5 acceptance criteria

- Successful APP-only build/flash, with splash, SD browser, touch controls and 3.5mm headphone autoswitch intact.
- Visual crisp pixel edges with no introduced colors (side-by-side vs known nearest-neighbour reference); report if `crisp-1to1` fails due to PPA input window geometry/alignment.
- Correct update when scrolling, changing cartridges, opening browser/menu, and when no tiles are dirty.
- Stable audio; compare the *same cartridge* with v0.65 for speed and spot missing/tearing pixels.
- Confirm transient-failure recovery logs, if any; **do not deliberately induce unstable PSRAM load** merely to trigger a failure.

After real hardware results are available, update Issue #1 with exact measurements/limitations, crediting the reporter and avoiding any untested performance claims.
