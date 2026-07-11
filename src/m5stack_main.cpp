#include <Arduino.h>
/*
 * Created by Layer8
 * M5Stack CoreS3 entry point and hardware abstraction layer for RONTO8.
 */
#include <M5Unified.h>

SemaphoreHandle_t flipSem;
SemaphoreHandle_t doneSem;
uint16_t* line_buffer[2];
int render_idx = 0;

void flipTask(void *pvParameters) {
    while (1) {
        xSemaphoreTake(flipSem, portMAX_DELAY);
        if (render_idx >= 0 && line_buffer[0] != NULL) {
            M5.Display.pushImageDMA(76, 32, 192, 192, line_buffer[render_idx]);
        }
        render_idx = 1 - render_idx;
        xSemaphoreGive(doneSem);
    }
}



#include <SPI.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include <math.h>

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
    uint16_t bg_gray = M5.Display.color565(48, 48, 48);
    
    // 中央のファイル表示部分をゲーム画面と完全一致 (x=74, y=28)
    M5.Display.fillRect(74, 28, 192, 192, bg_gray);

    uint16_t r4_yellow = M5.Display.color565(253, 192, 0);
    M5.Display.fillRect(0, 0, 320, 28, r4_yellow);
    M5.Display.setTextColor(TFT_BLACK, r4_yellow);

    M5.Display.setCursor(10, 9);
    M5.Display.setTextSize(2); // フォントサイズを2に戻す
    M5.Display.println("R8 BROWSER");

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, bg_gray);
    M5.Display.setCursor(79, 34); 
    M5.Display.println(g_browser_path);

    int visible = 9; // 画面内に収まるよう調整
    int start = 0;
    if (g_browser_count > visible) {
        if (g_browser_cursor >= visible) start = g_browser_cursor - visible + 1;
        if (start + visible > g_browser_count) start = g_browser_count - visible;
    }

    M5.Display.setTextSize(2); // フォントサイズを2に戻す
    int y_pos = 44; // スタート位置を少し下げる
    for (int i = start; i < start + visible && i < g_browser_count; i++) {
        M5.Display.setCursor(79, y_pos); 
        String name = g_browser_name_buf + g_browser_name_offsets[i];
        if (g_browser_is_dir[i]) {
            name = "[" + name + "]";
        }
        
        // はみ出ないように15文字で折り返し防止
        if (name.length() > 15) {
            name = name.substring(0, 15);
        }
        
        if (i == g_browser_cursor) {
            M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
            while (name.length() < 15) name += " ";
        } else {
            M5.Display.setTextColor(TFT_WHITE, bg_gray);
        }
        
        M5.Display.println(name);
        y_pos += 20; // 行間を20pxに設定（フォントサイズ2でも被らない距離）
    }
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
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
    uint16_t color_dpad = M5.Display.color565(180, 180, 180);
    uint16_t color_btn  = M5.Display.color565(220, 220, 220);

    // --- D-Pad (左端の黒帯エリアに寄せる) ---
    int cx = 35, cy = 180;
    M5.Display.drawRoundRect(cx - 15, cy - 35, 30, 30, 4, color_dpad); // UP
    M5.Display.drawRoundRect(cx - 15, cy + 5,  30, 30, 4, color_dpad); // DOWN
    M5.Display.drawRoundRect(cx - 35, cy - 15, 30, 30, 4, color_dpad); // LEFT
    M5.Display.drawRoundRect(cx + 5,  cy - 15, 30, 30, 4, color_dpad); // RIGHT
    M5.Display.drawCircle(cx, cy, 4, color_dpad); // CENTER

    // --- STARTボタン ---
    // さらに2ドット右、16ドット下
    int sx = 273, sy = 182;
    M5.Display.drawRoundRect(sx, sy, 45, 20, 4, color_dpad); 
    M5.Display.setCursor(sx + 8, sy + 6);
    M5.Display.setTextColor(color_dpad);
    M5.Display.setTextSize(1);
    M5.Display.print("START");

    // --- ボリュームボタン (カーソルの上) ---
    // さらに2px下に移動
    int vx = 35;
    M5.Display.drawTriangle(vx, 56, vx - 10, 72, vx + 10, 72, color_dpad); // UP
    M5.Display.drawTriangle(vx, 102, vx - 10, 86, vx + 10, 86, color_dpad); // DOWN

    // --- アクションボタン (右上エリアに配置) ---
    uint16_t color_btn_O = g_held_O ? M5.Display.color565(50, 50, 50) : M5.Display.color565(180, 180, 180);
    uint16_t color_bg_O  = g_held_O ? M5.Display.color565(180, 180, 180) : TFT_BLACK;

    int ox = 295, oy = 50; // Oボタン
    M5.Display.fillCircle(ox, oy, 20, color_bg_O);
    M5.Display.drawCircle(ox, oy, 20, color_btn_O);
    M5.Display.drawCircle(ox, oy, 18, color_btn_O);
    M5.Display.drawCircle(ox, oy, 10, color_btn_O); // なかの〇をラインで

    uint16_t color_btn_X = g_held_X ? M5.Display.color565(50, 50, 50) : M5.Display.color565(180, 180, 180);
    uint16_t color_bg_X  = g_held_X ? M5.Display.color565(180, 180, 180) : TFT_BLACK;

    int xx = 295, xy = 126; // Xボタン
    M5.Display.fillCircle(xx, xy, 20, color_bg_X);
    M5.Display.drawCircle(xx, xy, 20, color_btn_X);
    M5.Display.drawCircle(xx, xy, 18, color_btn_X);
    // なかの×をラインで
    M5.Display.drawLine(xx - 7, xy - 7, xx + 7, xy + 7, color_btn_X);
    M5.Display.drawLine(xx - 7, xy + 7, xx + 7, xy - 7, color_btn_X);
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

        // --- 左端エリア (D-Pad + Volume) ---
        if (tx < 120) {
            if (ty > 120) {
                int cx = 35, cy = 180; // 描画位置と合わせる
                int dx = tx - cx;
                int dy = ty - cy;
                
                if (dx*dx + dy*dy > 5*5) { // デッドゾーンを極小化して抜けを防止！
                    float angle = atan2f((float)dy, (float)dx) * (180.0f / (float)M_PI);
                    
                    // マリオ対策：上下左右のストライクゾーンを広め、斜めを狭くする
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
            } else if (ty > 46 && ty <= 81) {
                btn_state |= (1 << 8); // VOL_UP
            } else if (ty > 81 && ty <= 120) {
                btn_state |= (1 << 7); // VOL_DOWN
            }
        }
        // --- STARTボタン ---
        // 新しい位置: x=271, y=162 -> さらに16ドット下へ
        else if (tx >= 250 && tx <= 320 && ty >= 161 && ty <= 211) {
            btn_state |= (1 << 6);
        }
        // --- アクションボタン (右上エリア) ---
          else if (tx >= 250 && ty < 160) {
            int dxO = tx - 295, dyO = ty - 50;
            int dxX = tx - 295, dyX = ty - 126;
            int dO = dxO*dxO + dyO*dyO;
            int dX = dxX*dxX + dyX*dyX;
            
            if (dO < 38*38) touch_O = true;
            if (dX < 38*38) touch_X = true;
            if (dO < min_dist_O) min_dist_O = dO;
            if (dX < min_dist_X) min_dist_X = dX;
            
            // Bダッシュジャンプ (意図的に真ん中を押した時だけ両方ON)
            if (tx >= 250 && tx <= 320 && ty >= 80 && ty <= 96) {
                touch_O = true;
                touch_X = true;
            }
        }
    }

    // O button long press logic (only toggle if touch is closer to O)
    bool active_O = touch_O && (min_dist_O <= min_dist_X);
    static uint32_t press_start_time_O = 0;
    static bool prev_active_O = false;
    static bool toggled_O_this_press = false;
    if (active_O) {
        if (!prev_active_O) {
            press_start_time_O = millis();
            toggled_O_this_press = false;
            // Immediate release upon touch if it's currently held
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
            // Immediate release upon touch if it's currently held
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

static void audio_task(void *pvParameters)
{
    const int sample_count = 256;
    int16_t *audio_buf[2];
    audio_buf[0] = (int16_t *)malloc(sample_count * sizeof(int16_t));
    audio_buf[1] = (int16_t *)malloc(sample_count * sizeof(int16_t));
    if (!audio_buf[0] || !audio_buf[1]) {
        if (audio_buf[0]) free(audio_buf[0]);
        if (audio_buf[1]) free(audio_buf[1]);
        vTaskDelete(NULL);
        return;
    }

    int current_buf = 0;
    while (1) {
        render_sounds(audio_buf[current_buf], sample_count);

        while (!M5.Speaker.playRaw(audio_buf[current_buf], sample_count, 22050, false, 1, 0)) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        current_buf = 1 - current_buf;
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
            xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 5, NULL, 0);
            M5.Display.fillScreen(TFT_BLACK);
            draw_virtual_gamepad();
        }
    }
    
    extern uint16_t m_buttons[8];
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
    
    for (int y = 0; y < 192; y++) {
        for (int x = 0; x < 192; x++) {
            uint8_t c;
            int src_x = (x * 128) / 192;
            int src_y = (y * 128) / 192;
            if (src_x % 2 == 0) {
                c = vram[(src_y * 64) + (src_x / 2)] & 0x0F;
            } else {
                c = (vram[(src_y * 64) + (src_x / 2)] >> 4) & 0x0F;
            }
            line_buffer[render_idx][y * 192 + x] = m_colors[c];
        }
    }
    
    xSemaphoreGive(flipSem);
}

// ---------------------------------------------------------------------------
// Splash screen
// ---------------------------------------------------------------------------

void show_ronto8_splash() {
    uint16_t r4_yellow = M5.Display.color565(253, 192, 0);
    M5.Display.fillScreen(r4_yellow);
    M5.Display.setTextColor(TFT_BLACK, r4_yellow);
    
    // 1. タイトル部分
    M5.Display.setTextSize(4);
    M5.Display.setCursor(58, 10);
    M5.Display.print("R8");

    M5.Display.setTextSize(2);
    M5.Display.setCursor(58, 60);
    M5.Display.print("RONTO8 Core S3");

    // 2. 説明文（3行復活）
    M5.Display.setTextSize(1);
    M5.Display.setCursor(58, 95);
    M5.Display.println("RONTO8: A nu wave in the pico8 console.");
    M5.Display.setCursor(58, 110);
    M5.Display.println("Powering beyond hardware limitations.");
    M5.Display.setCursor(58, 125);
    M5.Display.println("Acceler8 the portable coding.");

    // 3. ライセンス情報を一番下に配置
    M5.Display.setCursor(58, 220);
    M5.Display.println("(C)RONTO8. BASED ON FEMTO8 & ZEPTO8.");

    while (true) {
        M5.update();
        if (M5.Touch.getCount() > 0) break;
        
        // 文字の点滅処理
        if (millis() % 1000 < 500) M5.Display.setTextColor(TFT_BLACK, r4_yellow);
        else M5.Display.setTextColor(r4_yellow, r4_yellow);
        
        M5.Display.setTextSize(2);
        M5.Display.setCursor(60, 170); 
        M5.Display.print("PRESS SCREEN");
        delay(50);
    }
    
    // 最後に全消去してLoadingを表示
    M5.Display.fillScreen(r4_yellow);
    M5.Display.setTextColor(TFT_BLACK, r4_yellow);
    M5.Display.setCursor(60, 180);
    M5.Display.print("Loading...          ");
}

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
        if (line_buffer[0]) memset(line_buffer[0], 0, 192 * 192 * 2);
        if (line_buffer[1]) memset(line_buffer[1], 0, 192 * 192 * 2);
    } else {
        g_emulator_init_failed = true;
    }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    
    Preferences prefs;
    prefs.begin("ronto8", false);
    g_volume = prefs.getInt("volume", 255);
    prefs.end();

    M5.Speaker.begin();
    M5.Speaker.setVolume(g_volume);
    M5.Speaker.setChannelVolume(0, 255); // Ensure channel 0 is at max volume

    M5.Display.fillScreen(TFT_BLACK);

    line_buffer[0] = (uint16_t*)heap_caps_malloc(192 * 192 * 2, MALLOC_CAP_SPIRAM);
    line_buffer[1] = (uint16_t*)heap_caps_malloc(192 * 192 * 2, MALLOC_CAP_SPIRAM);
    
    flipSem = xSemaphoreCreateBinary();
    doneSem = xSemaphoreCreateBinary();
    xSemaphoreGive(doneSem);
    xTaskCreatePinnedToCore(flipTask, "flipTask", 4096, NULL, configMAX_PRIORITIES - 2, NULL, 1);
    
    M5.Display.setColorDepth(16);
    M5.Display.setSwapBytes(true);


    // [重要] 速度を劇的に上げるため、超高速なSRAMにバッファを確保！
    p8_sprite.setPsram(false);
    p8_sprite.createSprite(128, 128);

    // CoreS3 SD card CS pin = 4
    SPI.begin(36, 35, 37, 4);
    if (!SD.begin(4, SPI, 25000000)) {
        M5.Display.println("SD Card Mount Failed");
        while (1) { delay(100); }
    }

    g_browser_name_buf     = (char*)    malloc(MAX_BROWSER_BUF_SIZE);
    g_browser_name_offsets = (uint16_t*)malloc(MAX_BROWSER_ENTRIES * sizeof(uint16_t));
    g_browser_is_dir       = (bool*)    malloc(MAX_BROWSER_ENTRIES * sizeof(bool));

    show_ronto8_splash();

    M5.Display.fillScreen(TFT_BLACK); // スプラッシュ後に一度だけ全体をクリア
    scan_browser_directory(g_browser_path);
    draw_browser();
    draw_virtual_gamepad(); // コントローラーの描画は最初の一回だけ
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
        xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 5, NULL, 0);
        M5.Display.fillScreen(TFT_BLACK);
        draw_virtual_gamepad();
    } else {
        // Game running
        p8_step();
    }}