#include <Arduino.h>
/*
 * Created by Layer8
 * M5Stack Tab5 entry point and hardware abstraction layer for RONTO8.
 */
#include <M5Unified.h>

SemaphoreHandle_t flipSem;
SemaphoreHandle_t doneSem;
uint16_t* line_buffer[2];
int render_idx = 0;

// レンダリングパラメータ (起動時のモード選択で確定)
int g_render_size = 128; // PICO-8 解像度 (PPAハードウェアスケーリングが720に拡大)
int g_render_x    = (1280 - 720) / 2; // PPA出力の水平位置 (Tab5用)
int g_render_y    = 0;   // PPA出力の Y オフセット

// PPA前方宣言
static bool ppa_present_region_scaled(int x, int y, int dst_width, int dst_height, int src_width, int src_height, const uint16_t *frame_buffer);

// --- スケーリング用変数 ---
static uint16_t* scaled_buf = nullptr;
static int src_idx[720];

void flipTask(void *pvParameters) {
    // スケーリング用の座標テーブルを事前計算
    for(int i = 0; i < 720; i++) {
        src_idx[i] = (i * 128) / 720;
    }
    // PSRAMに720x720の拡大用バッファを確保
    scaled_buf = (uint16_t*)heap_caps_malloc(720 * 720 * 2, MALLOC_CAP_SPIRAM);

    while (1) {
        xSemaphoreTake(flipSem, portMAX_DELAY);
        if (render_idx >= 0 && line_buffer[0] != NULL && scaled_buf != NULL) {
            uint16_t* src = line_buffer[render_idx];

            // Core 0 のパワーで一気に配列をコピー（超高速スケーリング）
            for (int y = 0; y < 720; y++) {
                uint16_t* src_row = &src[src_idx[y] * 128];
                uint16_t* dst_row = &scaled_buf[y * 720];
                for (int x = 0; x < 720; x++) {
                    dst_row[x] = src_row[src_idx[x]];
                }
            }
            // 拡大済みの画像をDMA転送
            M5.Display.pushImageDMA(g_render_x, g_render_y, 720, 720, scaled_buf);
        }
        render_idx = 1 - render_idx;
        xSemaphoreGive(doneSem);
    }
}

#include <FS.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/ppa.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

// PPA (Pixel Processing Accelerator) parameters for Tab5
static ppa_client_handle_t s_ppa_srm = nullptr;
static uint16_t *s_dsi_fb = nullptr;
static uint32_t s_dsi_fb_bytes = 0;
static int s_phys_w = 0;
static int s_phys_h = 0;
static bool s_ppa_ready = false;

static void init_ppa_fast_present(void) {
    s_ppa_ready = false;
    s_dsi_fb = nullptr;
    auto *panel_dev = M5.Display.getPanel();
    if (!panel_dev) return;

    auto *dsi = static_cast<lgfx::Panel_DSI *>(panel_dev);
    const auto detail = dsi->config_detail();
    const auto cfg = dsi->config();
    if (!detail.buffer || cfg.panel_width <= 0 || cfg.panel_height <= 0) return;

    s_dsi_fb = static_cast<uint16_t *>(detail.buffer);
    s_phys_w = cfg.panel_width;
    s_phys_h = cfg.panel_height;
    s_dsi_fb_bytes = (uint32_t)s_phys_w * (uint32_t)s_phys_h * sizeof(uint16_t);

    ppa_client_config_t client_cfg = {};
    client_cfg.oper_type = PPA_OPERATION_SRM;
    client_cfg.max_pending_trans_num = 1;
    client_cfg.data_burst_length = PPA_DATA_BURST_LENGTH_128;
    if (ppa_register_client(&client_cfg, &s_ppa_srm) != ESP_OK) {
        s_ppa_srm = nullptr;
        return;
    }
    s_ppa_ready = true;
}

static bool ppa_present_region_scaled(int x, int y, int dst_width, int dst_height, int src_width, int src_height, const uint16_t *frame_buffer) {
    if (!s_ppa_ready || !s_ppa_srm || !s_dsi_fb || !frame_buffer) return false;
    
    const int logical_w = M5.Display.width();
    const int logical_h = M5.Display.height();
    const int out_x = logical_h - (y + dst_height);
    const int out_y = x;
    if (out_x < 0 || out_y < 0 || out_x + dst_height > s_phys_w || out_y + dst_width > s_phys_h) return false;

    ppa_srm_oper_config_t op = {};
    op.in.buffer = (const void *)frame_buffer;
    op.in.pic_w = src_width;
    op.in.pic_h = src_height;
    op.in.block_w = src_width;
    op.in.block_h = src_height;
    op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    
    op.out.buffer = (void *)s_dsi_fb;
    op.out.pic_w = s_phys_w;
    op.out.pic_h = s_phys_h;
    op.out.block_offset_x = out_x;
    op.out.block_offset_y = out_y;
    op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

    op.rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
    op.scale_x = (float)dst_height / src_height;
    op.scale_y = (float)dst_width / src_width;

    return ppa_do_scale_rotate_mirror(s_ppa_srm, &op) == ESP_OK;
}
#include <Preferences.h>
#include <math.h>
// // #include <Bluepad32.h>

extern "C" {
#include "p8_emu.h"
#include "p8_parser.h"

// Expose internal functions
extern uint16_t m_colors[32];
extern void render_sounds(int16_t *buffer, int total_samples);
}

extern unsigned m_actual_fps;
static LGFX_Sprite p8_sprite(&M5.Display);

static constexpr int MAX_BROWSER_ENTRIES = 256;
static constexpr int MAX_BROWSER_BUF_SIZE = 4096;

static char*     g_browser_name_buf     = nullptr;
static uint16_t* g_browser_name_offsets = nullptr;
static bool*     g_browser_is_dir       = nullptr;
static int       g_browser_buf_used     = 0;

static String g_browser_path   = "/";
static int    g_browser_count  = 0;
static int    g_browser_cursor = 0;
static int    g_cursor_history[10] = {0};
static int    g_depth = 0;

String   get_browser_name(int index);
void     load_selected_rom();
uint16_t get_touch_state();
void     draw_virtual_gamepad();

static volatile bool g_emulator_ready       = false;
static volatile bool g_emulator_init_failed = false;
static volatile bool g_audio_task_started   = false;

int g_volume = 255;

// --- BLE ゲームパッド ---
static bool       g_bt_gamepad_mode = false;
static 

bool g_held_O = false;
bool g_held_X = false;

extern "C" {
    char g_last_error_message[256] = {0};
    bool g_emulator_crashed = false;
    char g_cart_title[32]   = {0};
    char g_cart_author[32]  = {0};
}

// ---------------------------------------------------------------------------
// SD / file browser
// ---------------------------------------------------------------------------

static bool is_rom_file(const String& name)
{
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".p8") || lower.endsWith(".png");
}

static void scan_browser_directory(const String& path)
{
    g_browser_count    = 0;
    g_browser_cursor   = 0;
    g_browser_buf_used = 0;

    File dir = SD.open(path.c_str());
    if (!dir || !dir.isDirectory())
        return;

    if (path != "/" && path != "/sd" && path != "/sd/" && g_browser_count < MAX_BROWSER_ENTRIES) {
        strcpy(g_browser_name_buf + g_browser_buf_used, "..");
        g_browser_name_offsets[g_browser_count] = g_browser_buf_used;
        g_browser_is_dir[g_browser_count]       = true;
        g_browser_buf_used += 3;
        g_browser_count++;
    }

    while (true) {
        File entry = dir.openNextFile();
        if (!entry)
            break;

        String name   = entry.name();

        if (name == "System Volume Information") {
            entry.close();
            continue;
        }

        bool   is_dir = entry.isDirectory();
        entry.close();

        if (name == "." || name == "..")
            continue;

        if (is_dir || is_rom_file(name)) {
            if (g_browser_count < MAX_BROWSER_ENTRIES) {
                int len = name.length();
                if (g_browser_buf_used + len + 1 < MAX_BROWSER_BUF_SIZE) {
                    strcpy(g_browser_name_buf + g_browser_buf_used, name.c_str());
                    g_browser_name_offsets[g_browser_count] = g_browser_buf_used;
                    g_browser_is_dir[g_browser_count]       = is_dir;
                    g_browser_buf_used += len + 1;
                    g_browser_count++;
                }
            }
        }
    }

    dir.close();
}

static void draw_browser()
{
    uint16_t brand_color = M5.Display.color565(255, 120, 0); // Orange
    uint16_t bg_gray = M5.Display.color565(30, 30, 30);
    uint16_t list_bg = M5.Display.color565(20, 20, 20);

    // Left and Right Bands are drawn by draw_virtual_gamepad now, but we'll draw them just in case
    
    draw_virtual_gamepad();

    // Center List Area Background
    M5.Display.fillRect(280, 0, 720, 720, list_bg);
    
    // Header
    M5.Display.fillRect(280, 0, 720, 70, bg_gray);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(brand_color, bg_gray);
    M5.Display.setCursor(300, 25);
    M5.Display.println(g_browser_path);

    int visible = 12;
    int start = 0;
    if (g_browser_count > visible) {
        if (g_browser_cursor >= visible) start = g_browser_cursor - visible + 1;
        if (start + visible > g_browser_count) start = g_browser_count - visible;
    }

    int y_pos = 90;
    M5.Display.setTextSize(3);
    for (int i = start; i < start + visible && i < g_browser_count; i++) {
        M5.Display.setCursor(300, y_pos);
        if (i == g_browser_cursor) {
            // Selected item highlighting
            M5.Display.fillRect(280, y_pos - 10, 720, 50, brand_color);
            M5.Display.setTextColor(TFT_BLACK, brand_color);
        } else {
            M5.Display.setTextColor(TFT_WHITE, list_bg);
        }
        
        char name_buf[40];
        strncpy(name_buf, g_browser_name_buf + g_browser_name_offsets[i], 39);
        name_buf[39] = ' ';
        
        if (g_browser_is_dir[i]) {
            M5.Display.print("[DIR]  ");
            M5.Display.println(name_buf);
        } else {
            M5.Display.print("       ");
            M5.Display.println(name_buf);
        }
        y_pos += 50;
    }
}

// ---------------------------------------------------------------------------
// IMU tilt input (D-pad via accelerometer)
// ---------------------------------------------------------------------------

#define IMU_THRESHOLD 0.3f // Tilt threshold (larger = less sensitive)

uint16_t get_imu_input()
{
    uint16_t btn_state = 0;
    float ax = 0, ay = 0, az = 0;
    
    M5.Imu.getAccelData(&ax, &ay, &az);
    
    // X-axis tilt - 右に傾けると ax が負になる
    if (ax < -IMU_THRESHOLD)      btn_state |= (1 << 1); // RIGHT
    else if (ax > IMU_THRESHOLD)  btn_state |= (1 << 0); // LEFT
    
    // Y-axis tilt (up/down)
    if (ay > IMU_THRESHOLD)       btn_state |= (1 << 3); // DOWN
    else if (ay < -IMU_THRESHOLD) btn_state |= (1 << 2); // UP

    return btn_state;
}

// ---------------------------------------------------------------------------
// Touch input
// ---------------------------------------------------------------------------

// CoreS3 display: 320 x 240
//
// Game area  : 192x192 centered at (160, 100)  (scale 1.5x, pivot 160,100)
// Left strip : x=[0,64]  (black border for D-pad)
// Right strip: x=[256,320] (black border for action buttons)
// Bottom strip: y=[200,240] (black border for START)
//
//   D-pad    : centre=(45, 180), deadzone=12
//   START    : rect x=[140,180], y=[210,228]
//   O button : centre=(295, 155), radius=22
//   X button : centre=(250, 195), radius=22
//
// Bit assignments:
//   bit 0=LEFT  bit 1=RIGHT  bit 2=UP  bit 3=DOWN
//   bit 4=O(ACTION1)  bit 5=X(ACTION2)  bit 6=START(PAUSE)

// ---------------------------------------------------------------------------
// 仮想コントローラーの描画（画面端へ移動）
// ---------------------------------------------------------------------------
void draw_virtual_gamepad()
{
    uint16_t brand_color = M5.Display.color565(255, 120, 0); // Orange
    uint16_t color_btn   = TFT_BLACK;
    uint16_t color_btn_p = M5.Display.color565(80, 80, 80);

    // --- ゲーム画面用のオレンジ帯背景描画 ---
    // (これがないとゲーム起動時に背景が黒のままになりボタンが見えなくなる)
    M5.Display.fillRect(0, 0, 280, 720, brand_color);
    M5.Display.fillRect(1000, 0, 280, 720, brand_color);

    // --- Top Left Logo (Graphic + Text Badge) ---
    int lx = 20, ly = 20;
    M5.Display.fillRoundRect(lx, ly, 220, 60, 30, color_btn);
    M5.Display.fillCircle(lx + 30, ly + 30, 20, brand_color); // Orange dot inside black pill
    M5.Display.setTextColor(brand_color, color_btn);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(lx + 65, ly + 20);
    M5.Display.print("Ronto 8");

    // --- D-Pad (Solid Black Cross) ---
    // マルチタッチ干渉を防ぐため、Y座標を意図的に上部に配置
    int cx = 136, cy = 350;
    M5.Display.fillRoundRect(cx - 25, cy - 85, 50, 170, 10, color_btn); // Vertical
    M5.Display.fillRoundRect(cx - 85, cy - 25, 170, 50, 10, color_btn); // Horizontal
    M5.Display.fillCircle(cx, cy, 20, M5.Display.color565(40,40,40));   // Center texture

    // --- Volume Buttons (Pills) ---
    M5.Display.fillRoundRect(80, 530, 100, 50, 25, color_btn); 
    M5.Display.fillRoundRect(80, 610, 100, 50, 25, color_btn); 
    M5.Display.setTextColor(brand_color);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(120, 545); M5.Display.print("+");
    M5.Display.setCursor(120, 625); M5.Display.print("-");

    // --- START Button (Graphic Icon instead of Text) ---
    M5.Display.fillRoundRect(1060, 20, 140, 60, 30, color_btn);
    // Play icon (triangle) for START
    M5.Display.fillTriangle(1115, 35, 1115, 65, 1145, 50, brand_color);

    // --- O / X (Symbols without text) ---
    // マルチタッチ干渉を防ぐため、Y座標を意図的に下部に配置
    int ox = 1100, oy = 600;
    int xx = 1200, xy = 460;

    uint16_t color_bg_O = g_held_O ? color_btn_p : color_btn;
    M5.Display.fillCircle(ox, oy, 60, color_bg_O);
    M5.Display.drawCircle(ox, oy, 25, brand_color);
    M5.Display.drawCircle(ox, oy, 24, brand_color);
    M5.Display.drawCircle(ox, oy, 26, brand_color);

    uint16_t color_bg_X = g_held_X ? color_btn_p : color_btn;
    M5.Display.fillCircle(xx, xy, 60, color_bg_X);
    for(int i=-3; i<=3; i++) {
        M5.Display.drawLine(xx - 25 + i, xy - 25, xx + 25 + i, xy + 25, brand_color);
        M5.Display.drawLine(xx + 25 + i, xy - 25, xx - 25 + i, xy + 25, brand_color);
    }
}

// ---------------------------------------------------------------------------
// タッチ判定の調整（新しいボタン位置に追従）
// ---------------------------------------------------------------------------
uint16_t get_touch_state()
{
    uint16_t btn_state = 0;
    bool touch_O = false;
    bool touch_X = false;
    int min_dist_O = 999999;
    int min_dist_X = 999999;
    auto count = M5.Touch.getCount();

    for (int i = 0; i < count; i++) {
        auto detail = M5.Touch.getDetail(i);
        if (!detail.isPressed()) continue;

        int tx = detail.x;
        int ty = detail.y;

        // --- 左側エリア (D-Pad + Volume) ---
        if (tx < 350) {
            if (ty < 500) { // 上部はD-Pad
                int cx = 136, cy = 350;
                int dx = tx - cx;
                int dy = ty - cy;
                if (dx*dx + dy*dy > 20*20) { // デッドゾーン
                    float angle = atan2f((float)dy, (float)dx) * (180.0f / (float)M_PI);
                    if (angle > -35.0f  && angle <  35.0f)   btn_state |= (1 << 1); // RIGHT
                    else if (angle >  145.0f || angle < -145.0f) btn_state |= (1 << 0); // LEFT
                    else if (angle >  55.0f  && angle <  125.0f) btn_state |= (1 << 3); // DOWN
                    else if (angle < -55.0f  && angle > -125.0f) btn_state |= (1 << 2); // UP
                    else {
                        if (angle >= 35.0f && angle <= 55.0f)     { btn_state |= (1 << 1); btn_state |= (1 << 3); }
                        if (angle >= 125.0f && angle <= 145.0f)   { btn_state |= (1 << 0); btn_state |= (1 << 3); }
                        if (angle <= -35.0f && angle >= -55.0f)   { btn_state |= (1 << 1); btn_state |= (1 << 2); }
                        if (angle <= -125.0f && angle >= -145.0f) { btn_state |= (1 << 0); btn_state |= (1 << 2); }
                    }
                }
            } else if (ty >= 500 && ty < 590) {
                btn_state |= (1 << 8); // VOL_UP
            } else if (ty >= 590) {
                btn_state |= (1 << 7); // VOL_DOWN
            }
        }
        // --- STARTボタン ---
        else if (tx >= 1000 && ty <= 150) {
            btn_state |= (1 << 6);
        }
        // --- アクションボタン (右側エリア) ---
        else if (tx >= 950) {
            int ox = 1100, oy = 600;
            int xx = 1200, xy = 460;
            
            int dxO = tx - ox, dyO = ty - oy;
            int dxX = tx - xx, dyX = ty - xy;
            int dO = dxO*dxO + dyO*dyO;
            int dX = dxX*dxX + dyX*dyX;
            
            if (dO < 90*90) touch_O = true;
            if (dX < 90*90) touch_X = true;
            if (dO < min_dist_O) min_dist_O = dO;
            if (dX < min_dist_X) min_dist_X = dX;
            
            // Bダッシュジャンプ用の中間タッチ判定 (意図的に両方の間を押した場合)
            if (tx >= 1050 && tx <= 1250 && ty >= 480 && ty <= 580) {
                touch_O = true;
                touch_X = true;
            }
        }
    }

    // O button long press logic
    bool active_O = touch_O && (min_dist_O <= min_dist_X);
    static uint32_t press_start_time_O = 0;
    static bool prev_active_O = false;
    static bool toggled_O_this_press = false;
    if (active_O) {
        if (!prev_active_O) {
            press_start_time_O = millis();
            toggled_O_this_press = false;
            if (g_held_O) {
                g_held_O = false;
                toggled_O_this_press = true;
            }
        } else if (!toggled_O_this_press && millis() - press_start_time_O >= 1000) {
            g_held_O = true;
            toggled_O_this_press = true;
        }
    } else {
        toggled_O_this_press = false;
    }
    prev_active_O = active_O;
    if (g_held_O || touch_O) btn_state |= (1 << 4);

    // X button long press logic
    bool active_X = touch_X && (min_dist_X <= min_dist_O);
    static uint32_t press_start_time_X = 0;
    static bool prev_active_X = false;
    static bool toggled_X_this_press = false;
    if (active_X) {
        if (!prev_active_X) {
            press_start_time_X = millis();
            toggled_X_this_press = false;
            if (g_held_X) {
                g_held_X = false;
                toggled_X_this_press = true;
            }
        } else if (!toggled_X_this_press && millis() - press_start_time_X >= 1000) {
            g_held_X = true;
            toggled_X_this_press = true;
        }
    } else {
        toggled_X_this_press = false;
    }
    prev_active_X = active_X;
    if (g_held_X || touch_X) btn_state |= (1 << 5);

    return btn_state;
}

// ---------------------------------------------------------------------------
// Audio task
// ---------------------------------------------------------------------------

static constexpr size_t kP8RingCount = 4;
static int16_t s_audio_ring[kP8RingCount][512];
static size_t s_audio_ring_pos = 0;

static void audio_task(void *pvParameters)
{
    const int sample_count = 512;
    while (1) {
        int16_t *dst = s_audio_ring[s_audio_ring_pos];
        render_sounds(dst, sample_count);

        int retry = 0;
        // 実績コードに合わせ、チャンネル0に固定し、ストリーミング再生(第7引数=false)
        // 実績コードに合わせて「チャンネル 1」を使用
        while (!M5.Speaker.playRaw(dst, sample_count, 22050, false, 1, 1, false)) {
            vTaskDelay(pdMS_TO_TICKS(1));
            if (++retry > 100) { break; }
        }
        s_audio_ring_pos = (s_audio_ring_pos + 1) % kP8RingCount;
    }
}

// ---------------------------------------------------------------------------
// Emulator C interface stubs
// ---------------------------------------------------------------------------

void process_volume_controls(uint16_t current_btn) {
    static uint16_t global_last_btn = 0;
    uint16_t global_pressed = current_btn & ~global_last_btn;
    global_last_btn = current_btn;
    bool vol_changed = false;
    if (global_pressed & (1 << 8)) { g_volume = (g_volume > 239) ? 255 : g_volume + 16; vol_changed = true; }
    if (global_pressed & (1 << 7)) { g_volume = (g_volume <  16) ?   0 : g_volume - 16; vol_changed = true; }

    if (vol_changed) {
        M5.Speaker.setVolume(g_volume);
        M5.Speaker.tone(1000, 10);
        Preferences prefs;
        prefs.begin("ronto8", false);
        prefs.putInt("volume", g_volume);
        prefs.end();
    }
}

extern "C" void m5stack_update_input()
{
    M5.update();

        // --- タッチ入力 (既存コード) ---
        uint16_t current_btn = get_touch_state();
        process_volume_controls(current_btn);

        static bool prev_held_O = false;
        static bool prev_held_X = false;
        if (g_emulator_ready && (g_held_O != prev_held_O || g_held_X != prev_held_X)) {
            prev_held_O = g_held_O;
            prev_held_X = g_held_X;
            draw_virtual_gamepad();
        }
        if (!g_emulator_ready) {
            g_emulator_ready = true;
            if (!g_audio_task_started) {
                g_audio_task_started = true;
                xTaskCreatePinnedToCore(audio_task, "audio_task", 8192, NULL, 7, NULL, 1);
                M5.Display.fillScreen(TFT_BLACK);
                draw_virtual_gamepad();
            }
        }
        extern uint16_t m_buttons[2]; // PLAYER_COUNT=2
        m_buttons[0] = current_btn;
}

extern "C" void m5stack_suspend_frame_buf(void) {}
extern "C" void m5stack_resume_frame_buf(void)  {}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// レンダリングの超高速化と画面サイズの縮小
// ---------------------------------------------------------------------------
extern "C" void p8_render()
{
    if (!m_memory || !line_buffer[0]) return;
    uint8_t* vram = &m_memory[0x6000]; // VRAM starts at 0x6000

    xSemaphoreTake(doneSem, portMAX_DELAY);
    M5.update(); // SAFE ZONE for I2C

    // 出力先バッファのポインタ
    uint16_t* dest = line_buffer[render_idx];

    // if文と割り算を排除した超高速ループ (2ピクセル同時処理)
    // 128x128固定出力、PPAが720x720にハードウェアスケーリング
    for (int y = 0; y < 128; y++) {
        int y_offset = y * 64;
        for (int x = 0; x < 64; x++) {
            uint8_t pair = vram[y_offset + x];
            *dest++ = m_colors[pair & 0x0F];
            *dest++ = m_colors[pair >> 4];
        }
    }

    xSemaphoreGive(flipSem);
}

// ---------------------------------------------------------------------------
// Splash screen — [START] バーチャルパッドあり / [BT Cnct] BLE ゲームパッド
// ---------------------------------------------------------------------------

enum SplashAction { SPLASH_START, SPLASH_BT };

SplashAction show_ronto8_splash() {
    uint16_t brand_color = M5.Display.color565(255, 120, 0); // Orange
    M5.Display.fillScreen(brand_color);
    
    // Premium Splash Badge
    int lx = 340, ly = 260;
    M5.Display.fillRoundRect(lx, ly, 600, 200, 100, TFT_BLACK); // Black Pill
    M5.Display.fillCircle(lx + 100, ly + 100, 70, brand_color); // Orange Inner Dot
    
    M5.Display.setTextColor(brand_color, TFT_BLACK);
    M5.Display.setTextSize(6);
    M5.Display.setCursor(lx + 210, ly + 80);
    M5.Display.print("Ronto 8");
    
    delay(2000);
    return SPLASH_START;
}

// ---------------------------------------------------------------------------
// BLE ゲームパッド ペアリング画面
// 成功 → true  タイムアウト/キャンセル → false (スプラッシュに戻る)
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// ROM loader
// ---------------------------------------------------------------------------

// ROM loading uses p8_init_file_with_param (declared in p8_emu.h)

String get_browser_name(int index)
{
    if (index < 0 || index >= g_browser_count) return "";
    return String(g_browser_name_buf + g_browser_name_offsets[index]);
}

void load_selected_rom()
{
    String filename = get_browser_name(g_browser_cursor);
    String fullpath = g_browser_path + filename;

    M5.Display.fillScreen(TFT_BLACK);

    // SD.open() uses "/" as root, but fopen() (used by the parser) needs the
    // VFS mount point which is "/sd" by default with SD.begin().
    static String current_rom_path;
    current_rom_path = "/sd" + fullpath;

    if (p8_init_file_with_param(current_rom_path.c_str(), NULL) == 0) {
        g_emulator_ready = true;
        // Clear the line buffers so the previous game's frame doesn't flash
        extern uint16_t *line_buffer[2];
        // 確保しているサイズをクリアする
        if (line_buffer[0]) memset(line_buffer[0], 0, 720 * 720 * 2);
        if (line_buffer[1]) memset(line_buffer[1], 0, 720 * 720 * 2);
    } else {
        g_emulator_init_failed = true;
    }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup()
{
    esp_log_level_set("*", ESP_LOG_NONE); // ログを無効化して高速化
    
    auto cfg = M5.config();
    M5.begin(cfg);
    
    if (M5.Display.width() < M5.Display.height()) {
        M5.Display.setRotation(1);
    }
    M5.Display.setSwapBytes(true);
    M5.Display.fillScreen(0x0000);
    init_ppa_fast_present();
    
    Preferences prefs;
    prefs.begin("ronto8", false);
    g_volume = prefs.getInt("volume", 255);
    prefs.end();

    if (M5.Speaker.isRunning()) {
        M5.Speaker.end();
    }
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.task_pinned_core = 1; // 実績コードに合わせて Core 1
    spk_cfg.task_priority = 8;
    spk_cfg.dma_buf_len = 512;
    spk_cfg.dma_buf_count = 8;
    M5.Speaker.config(spk_cfg);

    if (M5.Speaker.isEnabled()) {
        M5.Speaker.begin();
    }
    int safe_volume = (g_volume > 128) ? 128 : g_volume;
    M5.Speaker.setVolume(safe_volume);
    M5.Speaker.setChannelVolume(0, 255);
    M5.Speaker.setChannelVolume(1, 255); // チャンネル1の音量も最大に

    M5.Display.fillScreen(TFT_BLACK);

    // Tab5の解像度に合わせて最大サイズで確保
    line_buffer[0] = (uint16_t*)heap_caps_malloc(720 * 720 * 2, MALLOC_CAP_SPIRAM);
    line_buffer[1] = (uint16_t*)heap_caps_malloc(720 * 720 * 2, MALLOC_CAP_SPIRAM);
    
    flipSem = xSemaphoreCreateBinary();
    doneSem = xSemaphoreCreateBinary();
    xSemaphoreGive(doneSem);
    xTaskCreatePinnedToCore(flipTask, "flipTask", 4096, NULL, configMAX_PRIORITIES - 2, NULL, 0);
    
    M5.Display.setColorDepth(16);
    M5.Display.setSwapBytes(true);


    // [重要] 速度を劇的に上げるため、超高速なSRAMにバッファを確保！
    p8_sprite.setPsram(false);
    p8_sprite.createSprite(128, 128);

    // M5Stack Tab5 SD card pins: SCK=43, MISO=39, MOSI=44, CS=42
    SPI.begin(43, 39, 44, 42);
    if (!SD.begin(42, SPI, 25000000)) {
        M5.Display.println("SD Card Mount Failed");
        while (1) { delay(100); }
    }

    g_browser_name_buf     = (char*)    malloc(MAX_BROWSER_BUF_SIZE);
    g_browser_name_offsets = (uint16_t*)malloc(MAX_BROWSER_ENTRIES * sizeof(uint16_t));
    g_browser_is_dir       = (bool*)    malloc(MAX_BROWSER_ENTRIES * sizeof(bool));

    // ---- 起動モード選択 ----
    // Bluetooth機能は無効化されているため、常にタッチパッドモード
    g_bt_gamepad_mode = false;
    
    while (true) {
        SplashAction action = show_ronto8_splash();
        if (action == SPLASH_START) {
            Preferences prefs2;
            prefs2.begin("ronto8", false);
            prefs2.putBool("bt_mode", false);
            prefs2.end();
            uint16_t r4y = M5.Display.color565(253, 192, 0);
            M5.Display.fillScreen(r4y);
            M5.Display.setTextColor(TFT_BLACK, r4y);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(60, 180);
            M5.Display.print("Loading...");
            break;
        } else {
            // SPLASH_BT が選択された場合も無効メッセージを出して戻る
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(20, 100);
            M5.Display.print("BT Pad is disabled.");
            delay(1500);
        }
    }

    M5.Display.fillScreen(TFT_BLACK);
    scan_browser_directory(g_browser_path);
    draw_browser();
    draw_virtual_gamepad();
}

void process_volume_controls(uint16_t current_btn);

void loop()
{
    if (g_emulator_init_failed) {
        static bool shown = false;
        if (!shown) {
            shown = true;
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setCursor(0, 0);
            M5.Display.println("ROM init failed.");
            M5.Display.println("Please reset and choose another cart.");
        }
        return;
    }

    if (!g_emulator_ready) {
        M5.update();
        uint16_t touch_btn = get_touch_state();
        uint16_t current_btn = touch_btn;
        
        process_volume_controls(current_btn);

        // ROM browser: debounce with edge detection + repeat (long-press)
        static uint16_t last_btn = 0;
        static uint32_t hold_timer = 0;
        uint16_t pressed = current_btn & ~last_btn; // 押した瞬間の判定

        // リピート（長押し）の処理を追加
        if (current_btn == last_btn && current_btn != 0) {
            if (millis() - hold_timer > 250) { // 250ms長押しし続けたら
                pressed = current_btn;         // 再度押したことにして
                hold_timer = millis() - 200;   // 50ms間隔で連射させる
            }
        } else {
            hold_timer = millis(); // 離したり別のボタンを押したらタイマーリセット
        }
        last_btn = current_btn;

        bool cursor_moved = false;
        if (pressed & (1 << 3)) { // DOWN
            if (g_browser_cursor < g_browser_count - 1) {
                g_browser_cursor++;
                cursor_moved = true;
            }
        }
        if (pressed & (1 << 2)) { // UP
            if (g_browser_cursor > 0) {
                g_browser_cursor--;
                cursor_moved = true;
            }
        }
        if (pressed & (1 << 1)) { // RIGHT (Page Down)
            if (g_browser_cursor < g_browser_count - 1) {
                g_browser_cursor += 9;
                if (g_browser_cursor >= g_browser_count) g_browser_cursor = g_browser_count - 1;
                cursor_moved = true;
            }
        }
        if (pressed & (1 << 0)) { // LEFT (Page Up)
            if (g_browser_cursor > 0) {
                g_browser_cursor -= 9;
                if (g_browser_cursor < 0) g_browser_cursor = 0;
                cursor_moved = true;
            }
        }
        if (cursor_moved) {
            M5.Speaker.setVolume(g_volume / 2);
            M5.Speaker.tone(1500, 8); // ファイル選択時のクリック音 (音量50%)
            delay(10);
            M5.Speaker.setVolume(g_volume);
            draw_browser();
        }
        if (pressed & (1 << 5)) { // X = back
            if (g_browser_path != "/") {
                int last_slash = g_browser_path.lastIndexOf('/', g_browser_path.length() - 2);
                if (last_slash >= 0) {
                    g_browser_path = g_browser_path.substring(0, last_slash + 1);
                    if (g_depth > 0) g_depth--;
                    scan_browser_directory(g_browser_path);
                    g_browser_cursor = g_cursor_history[g_depth];
                    draw_browser();
                }
            }
        }
        if ((pressed & (1 << 4)) || (pressed & (1 << 6))) { // O or START = enter
            if (g_browser_is_dir[g_browser_cursor]) {
                if (g_depth < 9) {
                    g_cursor_history[g_depth] = g_browser_cursor;
                    g_depth++;
                }
                g_browser_path += get_browser_name(g_browser_cursor) + "/";
                scan_browser_directory(g_browser_path);
                draw_browser();
            } else {
                load_selected_rom();
            }
        }

    } else if (g_emulator_ready && !g_audio_task_started) {
        g_audio_task_started = true;
        xTaskCreatePinnedToCore(audio_task, "audio_task", 8192, NULL, 7, NULL, 1);
        M5.Display.fillScreen(TFT_BLACK);
        if (!g_bt_gamepad_mode) draw_virtual_gamepad();
    } else {
        // Game running at 60FPS
        static TickType_t xLastWakeTime = xTaskGetTickCount();
        const TickType_t xFrequency = pdMS_TO_TICKS(16); // 60FPS
        p8_step();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }}
