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

// レンダリングパラメータ (起動時のモード選択で確定)
int g_render_size = 192; // BT フルスクリーン時は 240
int g_render_x    = 76;  // 表示 X オフセット
int g_render_y    = 32;  // 表示 Y オフセット

void flipTask(void *pvParameters) {
    while (1) {
        xSemaphoreTake(flipSem, portMAX_DELAY);
        if (render_idx >= 0 && line_buffer[0] != NULL) {
            M5.Display.pushImageDMA(g_render_x, g_render_y,
                                    g_render_size, g_render_size,
                                    line_buffer[render_idx]);
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
#include <NimBLEDevice.h>
#include <NimBLEClient.h>

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

// ===========================================================================
// BLE HID ゲームパッド ホスト 実装 (NimBLE-Arduino)
// ===========================================================================
static bool       g_bt_gamepad_mode   = false;
static bool       g_bt_connected      = false;
static uint16_t   g_bt_state          = 0;   // 現在のボタン状態

// ゲームパッド HID レポートパースエンジン (標準HID Gamepad Reportに対応)
// Report: byte0=btns_low, byte1=btns_high, byte2=hatswitch, byte3=lx, byte4=ly
static void parse_hid_report(const uint8_t* data, size_t len) {
    if (len < 4) return;
    uint16_t btns = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    uint8_t  hat  = (len > 2) ? data[2] : 0xFF;
    int8_t   lx   = (len > 3) ? (int8_t)data[3] : 0;
    int8_t   ly   = (len > 4) ? (int8_t)data[4] : 0;

    uint16_t state = 0;
    // Hat switch (D-pad): 0=U, 1=UR, 2=R, 3=DR, 4=D, 5=DL, 6=L, 7=UL, 8=none
    if (hat == 0 || hat == 1 || hat == 7) state |= (1 << 2); // UP
    if (hat == 3 || hat == 4 || hat == 5) state |= (1 << 3); // DOWN
    if (hat == 1 || hat == 2 || hat == 3) state |= (1 << 1); // RIGHT
    if (hat == 5 || hat == 6 || hat == 7) state |= (1 << 0); // LEFT
    // 左スティックを D-pad にマッピング (-128〜127、非感帯を小さめに)
    if (lx < -40) state |= (1 << 0);
    if (lx >  40) state |= (1 << 1);
    if (ly < -40) state |= (1 << 2);
    if (ly >  40) state |= (1 << 3);
    // フェイスボタン (標準HIDゲームパッドレイアウト)
    // bit0=Cross/A  bit1=Circle/B  bit3=Square/X  bit4=Triangle/Y
    if (btns & (1 << 0)) state |= (1 << 4); // Cross   = O(ACTION1)
    if (btns & (1 << 1)) state |= (1 << 5); // Circle  = X(ACTION2)
    // Options/Start ボタンはコントローラーごとに当てが異なるので複数bitを確認
    if (btns & (1 << 9))  state |= (1 << 6); // Options(PS4) = PAUSE
    if (btns & (1 << 4))  state |= (1 << 6); // Start(Switch) = PAUSE
    if (btns & (1 << 7))  state |= (1 << 6); // Start(Xbox) = PAUSE

    g_bt_state = state;
}

// NimBLE チャラクタリスティック (HID Notify ハンドラ)
static NimBLEClient* g_ble_client = nullptr;

class BLEHidCallbacks : public NimBLEClientCallbacks {
    // NimBLE v1.x: onDisconnect は引数 1つ
    void onDisconnect(NimBLEClient* pClient) override {
        g_bt_connected = false;
        g_bt_state     = 0;
    }
};
static BLEHidCallbacks g_ble_callbacks;

static void ble_notify_cb(NimBLERemoteCharacteristic* pChar,
                           uint8_t* pData, size_t length, bool isNotify) {
    parse_hid_report(pData, length);
}

// BLE HID サービス UUID
#define HID_SERVICE_UUID        "1812"
#define HID_REPORT_UUID         "2A4D"

// HID デバイスに接続してノティファイアィを登録する
static bool ble_connect_to_gamepad(NimBLEAdvertisedDevice* dev) {
    g_ble_client = NimBLEDevice::createClient();
    g_ble_client->setClientCallbacks(&g_ble_callbacks, false);
    g_ble_client->setConnectionParams(12, 12, 0, 200);

    if (!g_ble_client->connect(dev)) {
        NimBLEDevice::deleteClient(g_ble_client);
        g_ble_client = nullptr;
        return false;
    }

    auto* svc = g_ble_client->getService(HID_SERVICE_UUID);
    if (!svc) {
        g_ble_client->disconnect();
        return false;
    }

    // HID Report キャラクタリスティックすべてにノティファイ 登録
    auto chars = svc->getCharacteristics(true);
    bool found = false;
    for (auto& c : *chars) {
        if (c->getUUID() == NimBLEUUID(HID_REPORT_UUID)) {
            if (c->canNotify()) {
                c->subscribe(true, ble_notify_cb);
                found = true;
            }
        }
    }
    if (!found) {
        g_ble_client->disconnect();
        return false;
    }

    g_bt_connected = true;
    return true;
}

// BLE スキャンコールバック: HID サービスを持つデバイスだけフィルタ (NimBLE v1.x API)
class BLEScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
    NimBLEAdvertisedDevice* found_device = nullptr;
    // v1.x では非 const
    void onResult(NimBLEAdvertisedDevice* adv) override {
        if (adv->isAdvertisingService(NimBLEUUID(HID_SERVICE_UUID))) {
            found_device = adv;
            NimBLEDevice::getScan()->stop();
        }
    }
};
static BLEScanCallbacks g_scan_cb;
// ===========================================================================

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

    if (g_bt_gamepad_mode) {
        // --- BLE ゲームパッド入力 ---
        // ノティファイハンドラで非同期更新される g_bt_state をそのまま使用
        if (!g_emulator_ready) {
            g_emulator_ready = true;
            if (!g_audio_task_started) {
                g_audio_task_started = true;
                xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 5, NULL, 0);
                M5.Display.fillScreen(TFT_BLACK);
                // BT モードでは draw_virtual_gamepad() を呼ばない
            }
        }
        extern uint16_t m_buttons[8];
        m_buttons[0] = g_bt_state;
    } else {
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
                xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 5, NULL, 0);
                M5.Display.fillScreen(TFT_BLACK);
                draw_virtual_gamepad();
            }
        }
        extern uint16_t m_buttons[8];
        m_buttons[0] = current_btn;
    }
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

    // g_render_size に合わせて 128x128 PICO-8 画面を拡大レンダリング
    // 通常モード: 192x192 / BT フルスクリーン: 240x240
    int sz = g_render_size;
    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            int src_x = (x * 128) / sz;
            int src_y = (y * 128) / sz;
            uint8_t c;
            if (src_x % 2 == 0) {
                c = vram[(src_y * 64) + (src_x / 2)] & 0x0F;
            } else {
                c = (vram[(src_y * 64) + (src_x / 2)] >> 4) & 0x0F;
            }
            line_buffer[render_idx][y * sz + x] = m_colors[c];
        }
    }

    xSemaphoreGive(flipSem);
}

// ---------------------------------------------------------------------------
// Splash screen — [START] バーチャルパッドあり / [BT Cnct] BLE ゲームパッド
// ---------------------------------------------------------------------------

enum SplashAction { SPLASH_START, SPLASH_BT };

SplashAction show_ronto8_splash() {
    uint16_t r4_yellow = M5.Display.color565(253, 192, 0);
    M5.Display.fillScreen(r4_yellow);
    M5.Display.setTextColor(TFT_BLACK, r4_yellow);

    // タイトル
    M5.Display.setTextSize(4);
    M5.Display.setCursor(58, 10);
    M5.Display.print("R8");

    M5.Display.setTextSize(2);
    M5.Display.setCursor(58, 60);
    M5.Display.print("RONTO8 Core S3");

    // 説明文
    M5.Display.setTextSize(1);
    M5.Display.setCursor(58, 95);
    M5.Display.println("RONTO8: A nu wave in the pico8 console.");
    M5.Display.setCursor(58, 110);
    M5.Display.println("Powering beyond hardware limitations.");
    M5.Display.setCursor(58, 125);
    M5.Display.println("Acceler8 the portable coding.");

    // ライセンス
    M5.Display.setCursor(58, 220);
    M5.Display.println("(C)RONTO8. BASED ON FEMTO8 & ZEPTO8.");

    // START ボタン (左, 紺)
    uint16_t col_start = M5.Display.color565(20, 20, 120);
    M5.Display.fillRoundRect(30, 158, 118, 44, 8, col_start);
    M5.Display.setTextColor(TFT_WHITE, col_start);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(50, 172);
    M5.Display.print("START");

    // BT Cnct ボタン (右, 深緑)
    uint16_t col_bt = M5.Display.color565(20, 100, 20);
    M5.Display.fillRoundRect(172, 158, 118, 44, 8, col_bt);
    M5.Display.setTextColor(TFT_WHITE, col_bt);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(178, 163);
    M5.Display.print("Bluetooth");
    M5.Display.setTextSize(2);
    M5.Display.setCursor(185, 178);
    M5.Display.print("Cnct");

    // タッチ待ちループ
    while (true) {
        M5.update();
        if (M5.Touch.getCount() > 0) {
            auto t = M5.Touch.getDetail(0);
            if (t.isPressed()) {
                int tx = t.x, ty = t.y;
                if (ty >= 158 && ty <= 202) {
                    if (tx >= 30  && tx <= 148) { delay(150); return SPLASH_START; }
                    if (tx >= 172 && tx <= 290) { delay(150); return SPLASH_BT;    }
                }
            }
        }
        delay(50);
    }
}

// ---------------------------------------------------------------------------
// BLE ゲームパッド ペアリング画面
// 成功 → true  タイムアウト/キャンセル → false (スプラッシュに戻る)
// ---------------------------------------------------------------------------

static bool do_bt_pairing() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(30, 65);
    M5.Display.print("BT Connecting...");
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(M5.Display.color565(200, 200, 200), TFT_BLACK);
    M5.Display.setCursor(10, 98);
    M5.Display.print("Put gamepad in pairing mode.");
    M5.Display.setCursor(10, 112);
    M5.Display.print("Waiting up to 10 seconds...");

    const uint32_t TIMEOUT_MS = 10000;
    uint32_t start_time = millis();

    // NimBLE スキャン開始 (HID デバイスを探す)
    g_scan_cb.found_device = nullptr;
    auto* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&g_scan_cb, false); // v1.x API
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(10, false); // 10秒スキャン

    // スキャン結果を待機しながらカウントダウンを表示 (start_time/TIMEOUT_MSは上で宣言済み)
    while (scan->isScanning() && (millis() - start_time < TIMEOUT_MS)) {
        int sec = (int)((TIMEOUT_MS - (millis() - start_time)) / 1000) + 1;
        M5.Display.setTextColor(M5.Display.color565(255, 200, 0), TFT_BLACK);
        M5.Display.setTextSize(4);
        M5.Display.setCursor(136, 148);
        M5.Display.printf("%2d", sec);
        M5.update();
        delay(200);
    }

    if (g_scan_cb.found_device) {
        // HID デバイス発見! 接続試行
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setTextSize(1);
        M5.Display.setCursor(10, 100);
        M5.Display.print("Found device. Connecting...");

        if (ble_connect_to_gamepad(g_scan_cb.found_device)) {

        // 接続成功!
            M5.Display.fillScreen(TFT_BLACK);
            M5.Display.setTextColor(M5.Display.color565(0, 220, 80), TFT_BLACK);
            M5.Display.setTextSize(2);
            M5.Display.setCursor(60, 110);
            M5.Display.print("Connected!");
            delay(1000);
            return true;
        }
    }
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(M5.Display.color565(220, 60, 60), TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(20, 65);
    M5.Display.print("Connection Failed");
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(10, 100);
    M5.Display.print("No gamepad found within 10s.");
    M5.Display.setCursor(10, 114);
    M5.Display.print("Tap OK to return to menu.");

    uint16_t col_ok = M5.Display.color565(80, 80, 80);
    M5.Display.fillRoundRect(110, 155, 100, 40, 8, col_ok);
    M5.Display.setTextColor(TFT_WHITE, col_ok);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(138, 167);
    M5.Display.print("OK");

    while (true) {
        M5.update();
        if (M5.Touch.getCount() > 0) {
            auto t = M5.Touch.getDetail(0);
            if (t.isPressed()) {
                int tx = t.x, ty = t.y;
                if (tx >= 110 && tx <= 210 && ty >= 155 && ty <= 195) {
                    delay(200);
                    return false;
                }
            }
        }
        delay(50);
    }
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

    // BT フルスクリーン(240x240)も考慮して最大サイズで確保
    line_buffer[0] = (uint16_t*)heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM);
    line_buffer[1] = (uint16_t*)heap_caps_malloc(240 * 240 * 2, MALLOC_CAP_SPIRAM);
    
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

    // ---- 起動モード選択: BT自動再接続 → スプラッシュ選択 ----
    {
        bool bt_initialized = false;

        // Preferences から前回 BT モード使用フラグを確認 → 自動再接続試行
        {
            Preferences prefs2;
            prefs2.begin("ronto8", false);
            bool was_bt = prefs2.getBool("bt_mode", false);
            prefs2.end();

            if (was_bt) {
                // NimBLE 初期化 + スキャンで自動再接続試行
                NimBLEDevice::init("");
                NimBLEDevice::setPower(ESP_PWR_LVL_P9);
                bt_initialized = true;

                // 再接続待機画面 (最大 5 秒)
                M5.Display.fillScreen(TFT_BLACK);
                M5.Display.setTextColor(M5.Display.color565(200, 200, 200), TFT_BLACK);
                M5.Display.setTextSize(1);
                M5.Display.setCursor(50, 105);
                M5.Display.print("Reconnecting BT gamepad...");

                g_scan_cb.found_device = nullptr;
                auto* scan2 = NimBLEDevice::getScan();
                scan2->setAdvertisedDeviceCallbacks(&g_scan_cb, false); // v1.x API
                scan2->setActiveScan(true);
                scan2->start(5, false);

                uint32_t rc = millis();
                while (scan2->isScanning() && (millis() - rc < 5000)) {
                    int rem = (int)((5000 - (millis() - rc)) / 1000) + 1;
                    M5.Display.setTextColor(M5.Display.color565(255, 200, 0), TFT_BLACK);
                    M5.Display.setTextSize(3);
                    M5.Display.setCursor(148, 128);
                    M5.Display.printf("%d ", rem);
                    M5.update();
                    delay(200);
                }

                if (g_scan_cb.found_device) {
                    if (ble_connect_to_gamepad(g_scan_cb.found_device)) {
                        g_bt_gamepad_mode = true;
                    }
                }
            }
        }

        // 自動再接続できなかった場合はスプラッシュ選択ループ
        if (!g_bt_gamepad_mode) {
            while (true) {
                SplashAction action = show_ronto8_splash();
                if (action == SPLASH_START) {
                    // 通常モード: bt_mode = false として保存
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
                    // BT Cnct: NimBLE 初期化 (初回のみ) してペアリング試行
                    if (!bt_initialized) {
                        NimBLEDevice::init("");
                        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
                        bt_initialized = true;
                    }
                    if (do_bt_pairing()) {
                        g_bt_gamepad_mode = true;
                        Preferences prefs2;
                        prefs2.begin("ronto8", false);
                        prefs2.putBool("bt_mode", true);
                        prefs2.end();
                        break;
                    }
                    // ペアリング失敗 → スプラッシュに戻る
                }
            }
        }
    }

    // BT フルスクリーンモード: 240x240 でゲーム画面を拡大表示
    if (g_bt_gamepad_mode) {
        g_render_size = 240;
        g_render_x    = (320 - 240) / 2; // = 40 (水平中央)
        g_render_y    = 0;
    }

    M5.Display.fillScreen(TFT_BLACK);
    scan_browser_directory(g_browser_path);
    draw_browser();
    if (!g_bt_gamepad_mode) {
        draw_virtual_gamepad(); // バーチャルパッドは通常モードのみ
    }
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
        if (!g_bt_gamepad_mode) draw_virtual_gamepad();
    } else {
        // Game running
        p8_step();
    }}