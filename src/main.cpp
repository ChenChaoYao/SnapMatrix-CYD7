#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <time.h>
#include <vector>
#include "DisplayDriver.h"
#include "VirtualMatrix.h"
#include "MoonrakerClient.h"
#include "WeatherService.h"
#include "TailscaleService.h"

// Hardware instances
LGFX lcd;
VirtualMatrix matrix;
MoonrakerClient moonraker;
WeatherService weatherService;
WebServer server(80);
Preferences preferences;

// Configuration
String printer_ip = "192.168.1.100";
String printer_model = "snapmaker_u1"; // "snapmaker_u1" or "ideaformer_ir3"
bool ts_enabled = false;
String ts_auth_key = "";
#ifdef BOARD_CYD_50
#define DEFAULT_TS_DEV_NAME "snapmatrix-cyd5"
#else
#define DEFAULT_TS_DEV_NAME "snapmatrix-cyd7"
#endif
String ts_dev_name = DEFAULT_TS_DEV_NAME;
String ts_subnet_router = "";
String display_text = "CONNECTING TO WI-FI...";
String current_display_state = "IDLE";
int x_pos = MATRIX_WIDTH;

// 各機型獨立設定結構與函式
struct PrinterProfileConfig {
    String ip;
    bool ts_enabled;
    String ts_auth_key;
    String ts_dev_name;
    String ts_subnet;
};

PrinterProfileConfig getProfileConfig(const String &model) {
    PrinterProfileConfig cfg;
    if (model == "ideaformer_ir3") {
        cfg.ip = preferences.getString("ir3_ip", "192.168.200.235");
        cfg.ts_enabled = preferences.getBool("ir3_ts_en", true);
        cfg.ts_auth_key = preferences.getString("ir3_ts_key", preferences.getString("ts_auth_key", ""));
        cfg.ts_dev_name = preferences.getString("ir3_ts_dev", DEFAULT_TS_DEV_NAME);
        cfg.ts_subnet = preferences.getString("ir3_ts_sub", "100.64.121.55");
    } else {
        cfg.ip = preferences.getString("u1_ip", preferences.getString("printer_ip", "192.168.3.122"));
        cfg.ts_enabled = preferences.getBool("u1_ts_en", false);
        cfg.ts_auth_key = preferences.getString("u1_ts_key", preferences.getString("ts_auth_key", ""));
        cfg.ts_dev_name = preferences.getString("u1_ts_dev", DEFAULT_TS_DEV_NAME);
        cfg.ts_subnet = preferences.getString("u1_ts_sub", "");
    }
    return cfg;
}

void saveProfileConfig(const String &model, const PrinterProfileConfig &cfg) {
    String prefix = (model == "ideaformer_ir3") ? "ir3" : "u1";
    preferences.putString((prefix + "_ip").c_str(), cfg.ip);
    preferences.putBool((prefix + "_ts_en").c_str(), cfg.ts_enabled);
    preferences.putString((prefix + "_ts_key").c_str(), cfg.ts_auth_key);
    preferences.putString((prefix + "_ts_dev").c_str(), cfg.ts_dev_name);
    preferences.putString((prefix + "_ts_sub").c_str(), cfg.ts_subnet);
}

void applyProfile(const String &model) {
    printer_model = model;
    preferences.putString("printer_model", printer_model);
    PrinterProfileConfig cfg = getProfileConfig(model);
    printer_ip = cfg.ip;
    ts_enabled = cfg.ts_enabled;
    ts_auth_key = cfg.ts_auth_key;
    ts_dev_name = cfg.ts_dev_name;
    ts_subnet_router = cfg.ts_subnet;
}

// UI & Animations
uint32_t current_text_color = 0x00FFFF;
uint32_t current_bg_color   = 0x003C00;
uint32_t current_bar_color  = 0x00FF00;
bool bg_enabled = false;
int text_brightness = 100;
int bg_brightness = 100;
int bar_brightness = 100;
bool is_fill_mode = false;
int sleep_timeout = 10;
unsigned long idle_start = 1;
bool in_screensaver = false;
bool manual_sleep = false;
bool is_updating = false;
bool dashboard_initialized = false;
bool standby_initialized = false;

// Backlight Auto Dimming Settings (30% after 1 minute in sleep mode)
const uint8_t BRIGHTNESS_NORMAL = 240; // 100% working brightness
const uint8_t BRIGHTNESS_DIM    = 72;  // 30% dim brightness
uint8_t current_lcd_brightness = BRIGHTNESS_NORMAL;
unsigned long screensaver_enter_time = 0;

// Particle System for Fireworks
#define NUM_PARTICLES 40
struct Particle { float x, y, vx, vy; int life; uint16_t color; };
Particle particles[NUM_PARTICLES];
bool firework_active = false;
float rocket_x, rocket_y, rocket_vy;
uint16_t current_firework_color;

uint16_t random_color() {
    return matrix.Color(random(100, 255), random(100, 255), random(100, 255));
}

void init_firework() {
    firework_active = true;
    rocket_x = random(4, MATRIX_WIDTH - 4);
    rocket_y = MATRIX_HEIGHT - 1;
    rocket_vy = -random(35, 55) / 100.0f;
    current_firework_color = random_color();
}

void explode(float x, float y, uint16_t color) {
    for (int i = 0; i < NUM_PARTICLES; i++) {
        particles[i].x = x; particles[i].y = y;
        float angle = (float)random(0, 360) * 3.14159f / 180.0f;
        float speed = (float)random(3, 16) / 10.0f;
        particles[i].vx = cos(angle) * speed;
        particles[i].vy = sin(angle) * speed;
        particles[i].life = random(15, 45);
        particles[i].color = color;
    }
}

void drawFireworks() {
    if (!firework_active) {
        if (random(0, 45) == 0) init_firework();
    } else {
        matrix.drawPixel((int)rocket_x, (int)rocket_y, current_firework_color);
        rocket_y += rocket_vy;
        rocket_vy += 0.025f;
        if (rocket_vy >= 0) {
            explode(rocket_x, rocket_y, current_firework_color);
            firework_active = false;
        }
    }
    for (int i = 0; i < NUM_PARTICLES; i++) {
        if (particles[i].life > 0) {
            matrix.drawPixel((int)particles[i].x, (int)particles[i].y, particles[i].color);
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].vy += 0.05f;
            particles[i].life--;
        }
    }
}

void drawHeatingBed(int center_x) {
    uint16_t bright_red = matrix.Color(255, 0, 0);
    uint16_t dark_red = matrix.Color(150, 0, 0);
    uint16_t bed_plate = matrix.Color(100, 20, 20);
    
    matrix.drawLine(center_x - 4, 6, center_x + 4, 6, bed_plate);
    matrix.drawLine(center_x - 4, 5, center_x + 4, 5, bed_plate);
    
    uint8_t wave_offset = (millis() / 450) % 3;
    for(int i=-2; i<=2; i+=2) {
        int x = center_x + i;
        for(int y=0; y<=4; y++) {
            if ((y + wave_offset) % 3 == 0) {
                matrix.drawPixel(x, y, bright_red);
            } else {
                matrix.drawPixel(x, y, dark_red);
            }
        }
    }
}

void drawNozzle(int center_x) {
    uint16_t gray = matrix.Color(128, 128, 128);
    uint16_t bright_red = matrix.Color(255, 0, 0);
    uint16_t heat_red = matrix.Color(220, 20, 20);
    
    // Top body
    matrix.drawLine(center_x - 3, 0, center_x + 3, 0, gray);
    matrix.drawLine(center_x - 3, 1, center_x + 3, 1, gray);
    matrix.drawLine(center_x - 2, 2, center_x + 2, 2, gray);
    // Heating block / tip glowing red
    matrix.drawLine(center_x - 1, 3, center_x + 1, 3, heat_red);
    matrix.drawPixel(center_x, 4, bright_red);
    
    // Melted filament dripping in red
    uint8_t drip = (millis() / 600) % 4;
    if (drip >= 1) matrix.drawPixel(center_x, 5, bright_red);
    if (drip >= 2) matrix.drawPixel(center_x, 6, bright_red);
    if (drip >= 3) matrix.drawPixel(center_x, 7, bright_red);
}

void drawLeveling(int center_x) {
    uint16_t bed_col = matrix.Color(40, 50, 65);
    uint16_t bed_touch = matrix.Color(0, 220, 255);
    uint16_t body_col = matrix.Color(0, 140, 255);
    uint16_t needle_col = matrix.Color(160, 200, 255);
    uint16_t tap_col = matrix.Color(255, 60, 60);    // 觸碰熱床時亮紅 (如 BLTouch 探針觸發)
    uint16_t ready_col = matrix.Color(0, 255, 120);  // 抬起移動時亮綠

    // 1. 熱床底線 (Y=7)
    matrix.drawLine(center_x - 4, 7, center_x + 4, 7, bed_col);

    // 2. 往返平移循環：由左到右 (0..6)，跳完後由右到左 (5..1)，共 12 步往返循環
    const int step_duration = 260; // 每個探測點停留 260ms
    unsigned long now = millis();
    int cycle_step = (now / step_duration) % 12;
    int pt_idx = (cycle_step <= 6) ? cycle_step : (12 - cycle_step);
    int probe_x = center_x - 3 + pt_idx;

    // 3. 指針上下跳動探測：
    // 前段抬起 (0~60ms) -> 中段下跳觸碰 (60~190ms) -> 後段收回抬起 (190~260ms)
    int step_phase = now % step_duration;
    bool is_down = (step_phase >= 60 && step_phase < 190);

    // 4. 探針本體固定座 (Y=0..2)
    matrix.drawLine(probe_x, 0, probe_x, 2, body_col);
    if (probe_x > 0) matrix.drawPixel(probe_x - 1, 1, body_col);
    if (probe_x < MATRIX_WIDTH - 1) matrix.drawPixel(probe_x + 1, 1, body_col);

    // 5. 活動指針上下跳動
    if (is_down) {
        // 向下探測：針身伸長至 Y=5，針尖下壓至 Y=6 觸碰熱床
        matrix.drawLine(probe_x, 3, probe_x, 5, needle_col);
        matrix.drawPixel(probe_x, 6, tap_col);
        matrix.drawPixel(probe_x, 7, bed_touch); // 熱床被觸摸點亮起感應光
    } else {
        // 向上縮回：針尖收回至 Y=4 綠燈準備平移
        matrix.drawPixel(probe_x, 3, needle_col);
        matrix.drawPixel(probe_x, 4, ready_col);
    }
}

void drawFlowCalibration(int center_x) {
    uint16_t gray = matrix.Color(160, 160, 160);
    uint16_t cyan = matrix.Color(0, 230, 255);
    uint16_t green = matrix.Color(50, 255, 100);
    uint16_t dark_gray = matrix.Color(50, 50, 50);

    // Nozzle tip
    matrix.drawLine(center_x - 2, 0, center_x + 2, 0, gray);
    matrix.drawLine(center_x - 1, 1, center_x + 1, 1, gray);
    matrix.drawPixel(center_x, 2, gray);

    // Bed surface line
    matrix.drawLine(center_x - 4, 7, center_x + 4, 7, dark_gray);

    // Dynamic extruded test line varying thickness/pattern
    uint8_t wave = (millis() / 150) % 5;
    for (int i = -3; i <= 3; i++) {
        uint8_t h = ((i + wave + 10) % 3) + 1;
        uint16_t col = (h >= 2) ? cyan : green;
        matrix.drawLine(center_x + i, 7 - h, center_x + i, 6, col);
    }
}

void drawPrintingIcon(int center_x) {
    uint16_t head_col = matrix.Color(180, 180, 180);
    uint16_t heat_col = matrix.Color(255, 0, 0);
    uint16_t layer_col = matrix.Color(0, 230, 255);
    uint16_t fresh_col = matrix.Color(100, 255, 100);

    // Moving nozzle head X: oscillates smoothly between center_x-2 and center_x+2 (8-step smooth cycle)
    int phase = (millis() / 200) % 8;
    int offset = (phase <= 4) ? (phase - 2) : (6 - phase); // -2, -1, 0, 1, 2, 1, 0, -1
    int nx = center_x + offset;

    // Printhead block
    matrix.drawLine(nx - 1, 0, nx + 1, 0, head_col);
    matrix.drawPixel(nx, 1, head_col);
    matrix.drawPixel(nx, 2, heat_col);

    // Fresh extruded bead
    matrix.drawPixel(nx, 3, fresh_col);

    // Printed model layers (Y=4, 5, 6)
    matrix.drawLine(center_x - 3, 6, center_x + 3, 6, layer_col);
    matrix.drawLine(center_x - 2, 5, center_x + 2, 5, layer_col);
    matrix.drawLine(center_x - 1, 4, center_x + 1, 4, layer_col);
}

void drawWeatherSun(int x) {
    uint16_t gold = matrix.Color(255, 200, 0);
    uint16_t orange = matrix.Color(255, 120, 0);
    matrix.fillRect(x + 2, 2, 3, 3, gold);
    matrix.drawPixel(x + 3, 0, orange);
    matrix.drawPixel(x + 3, 6, orange);
    matrix.drawPixel(x, 3, orange);
    matrix.drawPixel(x + 6, 3, orange);
    matrix.drawPixel(x + 1, 1, orange);
    matrix.drawPixel(x + 5, 1, orange);
    matrix.drawPixel(x + 1, 5, orange);
    matrix.drawPixel(x + 5, 5, orange);
}

void drawWeatherCloud(int x) {
    uint16_t white = matrix.Color(230, 240, 255);
    uint16_t cyan = matrix.Color(120, 180, 240);
    matrix.fillRect(x + 2, 1, 3, 2, white);
    matrix.fillRect(x + 1, 3, 5, 3, cyan);
    matrix.drawPixel(x + 6, 4, cyan);
    matrix.drawPixel(x + 0, 4, cyan);
}

void drawWeatherRain(int x) {
    drawWeatherCloud(x);
    uint16_t blue = matrix.Color(0, 180, 255);
    uint8_t step = (millis() / 250) % 3;
    if (step == 0) {
        matrix.drawPixel(x + 2, 6, blue);
        matrix.drawPixel(x + 5, 7, blue);
    } else if (step == 1) {
        matrix.drawPixel(x + 3, 6, blue);
        matrix.drawPixel(x + 6, 7, blue);
    } else {
        matrix.drawPixel(x + 1, 7, blue);
        matrix.drawPixel(x + 4, 6, blue);
    }
}

void drawWeatherThunder(int x) {
    drawWeatherCloud(x);
    uint8_t flash = (millis() / 200) % 4;
    uint16_t yellow = (flash == 0 || flash == 2) ? matrix.Color(255, 255, 50) : matrix.Color(255, 180, 0);
    // Lightning bolt
    matrix.drawPixel(x + 4, 3, yellow);
    matrix.drawPixel(x + 3, 4, yellow);
    matrix.drawPixel(x + 4, 4, yellow);
    matrix.drawPixel(x + 3, 5, yellow);
    matrix.drawPixel(x + 2, 6, yellow);
    matrix.drawPixel(x + 3, 6, yellow);
    matrix.drawPixel(x + 2, 7, yellow);
}

String get_status_chinese(const String& s) {
    if (s == "IDLE") return "待機中";
    if (s == "RUNNING") return "列印中";
    if (s == "PAUSE") return "已暫停";
    if (s == "FINISH") return "列印完成";
    if (s == "CANCELLED") return "已取消";
    if (s == "FAILED") return "列印失敗";
    if (s == "PREPARE") return "準備中";
    if (s == "HEATING_BED") return "熱床加熱中";
    if (s == "HEATING_NOZZLE") return "噴頭加熱中";
    if (s == "LEVELING") return "熱床調平中";
    if (s == "CALIBRATING_FLOW" || s == "FLOW_CALIBRATE") return "校準擠出流量中";
    if (s == "LOADING") return "進料中";
    if (s == "UNLOADING") return "退料中";
    if (s == "ERROR") return "耗材異常";
    return s;
}

String format_time(int total_minutes) {
    if (total_minutes >= 60) {
        int hours = total_minutes / 60;
        int mins = total_minutes % 60;
        if (mins == 0) return String(hours) + "小時";
        return String(hours) + "小時" + String(mins) + "分";
    } else {
        return String(total_minutes) + "分鐘";
    }
}

String format_time_ascii(int total_minutes) {
    if (total_minutes >= 60) {
        int hours = total_minutes / 60;
        int mins = total_minutes % 60;
        if (mins == 0) return String(hours) + " HR";
        return String(hours) + " HR " + String(mins) + " MIN";
    } else {
        return String(total_minutes) + " MIN";
    }
}

uint16_t scaleColor(uint32_t color24, int percent) {
    if (percent <= 0) return 0;
    uint8_t r = (color24 >> 16) & 0xFF;
    uint8_t g = (color24 >> 8) & 0xFF;
    uint8_t b = color24 & 0xFF;
    if (percent >= 100) return matrix.Color(r, g, b);
    uint8_t new_r = (r * percent) / 100;
    uint8_t new_g = (g * percent) / 100;
    uint8_t new_b = (b * percent) / 100;
    return matrix.Color(new_r, new_g, new_b);
}


void refreshDisplayText(const PrinterState& pState) {
    current_display_state = in_screensaver ? "IDLE" : pState.gcode_state;

    if (in_screensaver) {
        time_t now;
        time(&now);
        struct tm * timeinfo = localtime(&now);
        char buff[20];
        strftime(buff, sizeof(buff), "%H:%M", timeinfo);
        if (weatherService.data.valid) {
            display_text = String(buff) + "  " + String((int)round(weatherService.data.temp)) + "C " + String(weatherService.data.humidity) + "% " + weatherService.data.desc_en;
        } else {
            display_text = String(buff);
        }
    } else if (pState.filament_state.length() > 0) {
        current_display_state = pState.filament_state;
        display_text = pState.filament_message;
    } else if (pState.gcode_state == "FAILED") {
        display_text = "FAILED";
        if (pState.message.length() > 0) display_text += ": " + pState.message;
        display_text.toUpperCase();
    } else if (pState.gcode_state == "CANCELLED") {
        display_text = "STOPPED";
    } else if (pState.gcode_state == "PAUSE") {
        display_text = "PAUSED";
        if (pState.message.length() > 0) display_text += ": " + pState.message;
        display_text.toUpperCase();
    } else if (pState.gcode_state == "RUNNING") {
        String time_str = (pState.mc_remaining_time < 0) ? "ESTIMATING" : (format_time_ascii(pState.mc_remaining_time) + " LEFT");
        if (printer_model == "ideaformer_ir3") {
            String info_str = (pState.total_layer_num > 0) ? 
                ("LAYER " + String(pState.layer_num) + "/" + String(pState.total_layer_num)) : 
                ("FILAMENT: " + String(pState.filament_used_m, 1) + "m");
            display_text = "PRINTING: " + String(pState.mc_percent) + "% - " + time_str + " - " + info_str + " - BED: " + String(pState.bed_temper) + "C - NOZZLE: " + String(pState.nozzle_temper) + "C";
        } else {
            display_text = "PRINTING: " + String(pState.mc_percent) + "% - " + time_str + " - LAYER " + String(pState.layer_num) + "/" + String(pState.total_layer_num) + " - BED: " + String(pState.bed_temper) + "C - NOZZLE: " + String(pState.nozzle_temper) + "C";
        }
    } else if (pState.gcode_state == "PREPARE") {
        display_text = "PREPARING TO PRINT";
    } else if (pState.gcode_state == "HEATING_BED") {
        display_text = "HEATING BED: " + String(pState.bed_temper) + "C / " + String(pState.bed_target_temper) + "C";
    } else if (pState.gcode_state == "HEATING_NOZZLE") {
        display_text = "HEATING NOZZLE: " + String(pState.nozzle_temper) + "C / " + String(pState.nozzle_target_temper) + "C";
    } else if (pState.gcode_state == "LEVELING") {
        if (pState.bed_mesh_total > 0 && pState.bed_mesh_point > 0) {
            display_text = "LEVELING BED: " + String(pState.bed_mesh_point) + " / " + String(pState.bed_mesh_total);
        } else {
            display_text = "LEVELING BED";
        }
    } else if (pState.gcode_state == "CALIBRATING_FLOW") {
        int flow_pct = (int)round(pState.extrude_factor * 100.0f);
        display_text = "CALIBRATING FLOW (" + String(flow_pct) + "%)";
    } else if (pState.gcode_state == "IDLE") {
        if (weatherService.data.valid) {
            display_text = "READY TO PRINT - " + weatherService.data.city_en + ": " + String((int)round(weatherService.data.temp)) + "C " + String(weatherService.data.humidity) + "% " + weatherService.data.desc_en;
        } else {
            display_text = "READY TO PRINT!";
        }
    } else if (pState.gcode_state == "FINISH") {
        display_text = "PRINT COMPLETED";
    } else {
        display_text = pState.gcode_state;
    }
}

void drawMatrixContent(const PrinterState& pState) {
    // Auto-sleep when in IDLE / FINISH / CANCELLED / FAILED state
    bool is_idle_state = (pState.gcode_state == "IDLE" || pState.gcode_state == "FINISH" || pState.gcode_state == "CANCELLED" || pState.gcode_state == "FAILED");
    
    static String last_observed_state = "";
    static String last_observed_filament = "";
    static bool first_init = true;

    if (first_init || pState.gcode_state != last_observed_state || pState.filament_state != last_observed_filament) {
        first_init = false;
        last_observed_state = pState.gcode_state;
        last_observed_filament = pState.filament_state;
        x_pos = matrix.width();
        refreshDisplayText(pState);
        if (is_idle_state) {
            idle_start = millis();
        } else {
            idle_start = 0;
            manual_sleep = false;
            in_screensaver = false;
            dashboard_initialized = false;
            standby_initialized = false;
        }
    }
    
    if (is_idle_state && idle_start == 0) {
        idle_start = millis();
    }

    bool was_in_screensaver = in_screensaver;
    bool should_sleep = manual_sleep || (is_idle_state && sleep_timeout > 0 && idle_start > 0 && (millis() - idle_start >= (unsigned long)sleep_timeout * 60000UL));
    in_screensaver = should_sleep;

    if (!was_in_screensaver && in_screensaver) {
        // Just entered screensaver
        screensaver_enter_time = millis();
    } else if (was_in_screensaver && !in_screensaver) {
        // Just exited screensaver
        screensaver_enter_time = 0;
        if (current_lcd_brightness != BRIGHTNESS_NORMAL) {
            current_lcd_brightness = BRIGHTNESS_NORMAL;
            lcd.setBrightness(current_lcd_brightness);
            Serial.println("[Display] 離開休眠模式 -> 恢復 100% 亮度");
        }
    }

    // Auto Dimming: After 1 minute in screensaver mode, adjust brightness to 30%
    if (in_screensaver) {
        if (screensaver_enter_time > 0 && (millis() - screensaver_enter_time >= 60000UL)) {
            if (current_lcd_brightness != BRIGHTNESS_DIM) {
                current_lcd_brightness = BRIGHTNESS_DIM;
                lcd.setBrightness(current_lcd_brightness);
                Serial.println("[Display] 進入休眠滿 1 分鐘 -> 螢幕亮度自動調暗至 30%");
            }
        }
    }

    if (in_screensaver) {
        time_t now;
        time(&now);
        struct tm * timeinfo = localtime(&now);
        
        matrix.fillScreen(0);
        uint16_t active_bg_color = bg_enabled ? scaleColor(current_bg_color, bg_brightness) : 0;
        matrix.fillRect(0, 0, MATRIX_WIDTH, 8, active_bg_color);
        
        // Static perfectly centered bold modern 24-hour clock (26 dots wide, 3 dots margins)
        uint16_t t_col = scaleColor(0xFFFFFF, text_brightness);
        bool colon_on = ((millis() / 1000) % 2 == 0);
        matrix.drawClockTime(timeinfo->tm_hour, timeinfo->tm_min, colon_on, t_col, 0);

        // Weekday Accumulative Indicators on Leftmost (X=0) & Rightmost (X=31) Columns
        // Mon -> Row 0 (Y=0), Tue -> Row 0..1, ..., Sun -> Row 0..6. Resets on Monday.
        // Thursday (Row 3): 2 horizontal dots (X=0,1 and X=30,31)
        // Colorful Spectrum: Mon(Sky), Tue(Cyan), Wed(Green), Thu(Gold), Fri(Orange), Sat(Pink/Purple), Sun(Red)
        static const uint32_t wday_palette[7] = {
            0x38BDF8, // 星期一: 科技天藍
            0x06B6D4, // 星期二: 電光水青
            0x10B981, // 星期三: 翡翠翠綠
            0xFACC15, // 星期四: 璀璨金黃 (雙燈)
            0xFB923C, // 星期五: 晚霞烈橙
            0xE879F9, // 星期六: 霓虹紫粉
            0xF87171  // 星期日: 經典假日紅
        };

        int current_wday_idx = (timeinfo->tm_wday == 0) ? 6 : (timeinfo->tm_wday - 1);
        for (int row = 0; row <= current_wday_idx; row++) {
            uint16_t row_col = scaleColor(wday_palette[row], text_brightness);
            if (row == 3) { // 星期四 (第 4 列 Y=3)：橫向兩顆
                matrix.drawPixel(0, 3, row_col);
                matrix.drawPixel(1, 3, row_col);
                matrix.drawPixel(MATRIX_WIDTH - 2, 3, row_col); // X=30
                matrix.drawPixel(MATRIX_WIDTH - 1, 3, row_col); // X=31
            } else {
                matrix.drawPixel(0, row, row_col);
                matrix.drawPixel(MATRIX_WIDTH - 1, row, row_col);
            }
        }

        // Bottom row (Y=7): 60-Second LED Progress Bar (Right-to-Left, 0 to 32 dots)
        int sec = timeinfo->tm_sec;
        int dots = ((sec + 1) * MATRIX_WIDTH) / 60;
        uint16_t sec_bar_col = scaleColor(0x0284C7, text_brightness); // 科技藍本體
        uint16_t sec_head_col = scaleColor(0x38BDF8, text_brightness); // 前緣光標亮點
        for (int i = 0; i < dots && i < MATRIX_WIDTH; i++) {
            int x = MATRIX_WIDTH - 1 - i;
            matrix.drawPixel(x, 7, (i == dots - 1) ? sec_head_col : sec_bar_col);
        }
        return;
    }

    matrix.fillScreen(0);
    uint16_t scaled_bg = scaleColor(current_bg_color, bg_brightness);
    uint16_t active_bg_color = bg_enabled ? scaled_bg : 0;

    if (in_screensaver) {
        matrix.fillRect(0, 0, MATRIX_WIDTH, 7, active_bg_color);
    } else if (pState.gcode_state == "RUNNING" || pState.gcode_state == "PAUSE") {
        int bar_width = map(pState.mc_percent, 0, 100, 0, MATRIX_WIDTH);
        if (is_fill_mode) {
            matrix.fillRect(0, 0, MATRIX_WIDTH, 7, 0);
            uint16_t fill_bar_color = scaleColor(current_bar_color, bar_brightness);
            matrix.fillRect(0, 0, bar_width, 7, fill_bar_color);
        } else {
            matrix.fillRect(0, 0, MATRIX_WIDTH, 7, active_bg_color);
        }
        uint32_t base_bar_color = (pState.gcode_state == "PAUSE") ? 0xFFA500 : current_bar_color;
        uint16_t scaled_bar_color = scaleColor(base_bar_color, bar_brightness);
        matrix.drawLine(0, 7, bar_width - 1, 7, scaled_bar_color);
    } else {
        matrix.fillRect(0, 0, MATRIX_WIDTH, 7, active_bg_color);
    }

    if (pState.gcode_state == "FINISH") {
        drawFireworks();
    }

    int fixed_icon_width = 0;

    // 1. DRAW FIXED LEADING ICON ON THE LEFT (X = 0..8)
    if (in_screensaver || current_display_state == "IDLE") {
        if (weatherService.data.valid) {
            fixed_icon_width = 10;
            if (weatherService.data.code >= 95) {
                drawWeatherThunder(1);
            } else if (weatherService.data.code >= 51 && weatherService.data.code <= 82) {
                drawWeatherRain(1);
            } else if (weatherService.data.code <= 1) {
                drawWeatherSun(1);
            } else {
                drawWeatherCloud(1);
            }
        }
    } else if (current_display_state == "CANCELLED") {
        fixed_icon_width = 10;
        uint8_t pulse = (sin(millis() / 150.0f) * 80) + 175;
        uint16_t red = matrix.Color(pulse, 0, 0);
        matrix.drawLine(2, 0, 4, 0, red);
        matrix.drawLine(1, 1, 5, 1, red);
        matrix.fillRect(0, 2, 7, 3, red);
        matrix.drawLine(1, 5, 5, 5, red);
        matrix.drawLine(2, 6, 4, 6, red);
        matrix.fillRect(3, 1, 1, 3, matrix.Color(255,255,255));
        matrix.drawPixel(3, 5, matrix.Color(255,255,255));
    } else if (current_display_state == "FAILED") {
        fixed_icon_width = 10;
        uint8_t pulse = (sin(millis() / 150.0f) * 80) + 175;
        uint16_t yellow = matrix.Color(pulse, pulse, 0);
        uint16_t black = matrix.Color(0, 0, 0);
        matrix.drawPixel(4, 0, yellow);
        matrix.drawLine(3, 1, 5, 1, yellow);
        matrix.drawLine(2, 2, 6, 2, yellow);
        matrix.drawLine(1, 3, 7, 3, yellow);
        matrix.drawLine(0, 4, 8, 4, yellow);
        matrix.drawLine(0, 5, 8, 5, yellow);
        matrix.drawLine(0, 6, 8, 6, yellow);
        matrix.drawLine(4, 2, 4, 3, black);
        matrix.drawPixel(4, 5, black);
    } else if (current_display_state == "LOADING" || current_display_state == "UNLOADING") {
        fixed_icon_width = 10;
        uint16_t cyan = matrix.Color(0, 255, 255);
        if (current_display_state == "LOADING") {
            matrix.drawLine(1, 2, 7, 2, cyan);
            matrix.drawLine(2, 3, 6, 3, cyan);
            matrix.drawLine(3, 4, 5, 4, cyan);
            matrix.drawPixel(4, 5, cyan);
            matrix.drawLine(4, 0, 4, 1, cyan);
        } else {
            matrix.drawPixel(4, 0, cyan);
            matrix.drawLine(3, 1, 5, 1, cyan);
            matrix.drawLine(2, 2, 6, 2, cyan);
            matrix.drawLine(1, 3, 7, 3, cyan);
            matrix.drawLine(4, 4, 4, 5, cyan);
        }
    } else if (current_display_state == "PAUSE") {
        fixed_icon_width = 9;
        uint8_t pulse = (sin(millis() / 200.0f) * 100) + 155;
        uint16_t orange = matrix.Color(pulse, pulse * 0.5f, 0);
        matrix.fillRect(1, 1, 2, 5, orange);
        matrix.fillRect(5, 1, 2, 5, orange);
    } else if (current_display_state == "PREPARE" || current_display_state == "HEATING_BED") {
        fixed_icon_width = 10;
        drawHeatingBed(4);
    } else if (current_display_state == "HEATING_NOZZLE") {
        fixed_icon_width = 10;
        drawNozzle(4);
    } else if (current_display_state == "LEVELING") {
        fixed_icon_width = 10;
        drawLeveling(4);
    } else if (current_display_state == "CALIBRATING_FLOW") {
        fixed_icon_width = 10;
        drawFlowCalibration(4);
    } else if (current_display_state == "RUNNING") {
        fixed_icon_width = 10;
        drawPrintingIcon(4);
    }

    uint32_t active_text_color = current_text_color;
    if (current_display_state == "FAILED") active_text_color = 0xFF0000;
    else if (current_display_state == "CANCELLED" || current_display_state == "PAUSE") active_text_color = 0xFFA500;
    else if (current_display_state == "FINISH") active_text_color = 0x00FF00;
    else if (current_display_state == "CALIBRATING_FLOW") active_text_color = 0x00E5FF;
    else if (current_display_state == "FILAMENT" || current_display_state == "LOADING" || current_display_state == "UNLOADING") active_text_color = 0x00FFFF;
    else if (display_text.indexOf("READY TO PRINT") >= 0) active_text_color = 0xFFFFFF;

    // 2. RENDER SCROLLING TEXT IN CLIPPED RIGHT VIEWPORT (Never overwrites the fixed icon!)
    matrix.setClip(fixed_icon_width, MATRIX_WIDTH - 1);
    matrix.setTextColor(scaleColor(active_text_color, text_brightness));
    matrix.setCursor(x_pos, 0);
    matrix.print(display_text);
    matrix.resetClip();

    int text_width = display_text.length() * 6;

    static unsigned long last_scroll_step = 0;
    if (millis() - last_scroll_step >= 80) { // Slower, elegant scroll speed
        last_scroll_step = millis();
        x_pos--;
        // When text has scrolled past the fixed icon boundary, restart from right edge
        if (x_pos < fixed_icon_width - text_width) {
            x_pos = matrix.width();
            refreshDisplayText(pState);
        }
    }
}

// 輔助函數：帶字距（Tracking / Letter-spacing）的置中文字繪製，防止中文字體粘連
static void drawSpacedStringCenter(LGFX& gfx, const String& str, int32_t cx, int32_t cy, int32_t letter_spacing = 6) {
    std::vector<String> chars;
    const char* p = str.c_str();
    while (*p) {
        int len = 1;
        unsigned char c = (unsigned char)*p;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        chars.push_back(String(p).substring(0, len));
        p += len;
    }
    if (chars.empty()) return;

    int32_t total_w = 0;
    std::vector<int32_t> widths;
    for (const auto& ch : chars) {
        int32_t w = gfx.textWidth(ch.c_str());
        widths.push_back(w);
        total_w += w;
    }
    total_w += (int32_t)(chars.size() - 1) * letter_spacing;

    int32_t cur_x = cx - total_w / 2;
    textdatum_t old_datum = gfx.getTextDatum();
    gfx.setTextDatum(textdatum_t::middle_left);
    for (size_t i = 0; i < chars.size(); ++i) {
        gfx.drawString(chars[i], cur_x, cy);
        cur_x += widths[i] + letter_spacing;
    }
    gfx.setTextDatum(old_datum);
}

// 輔助函數：帶字距的左對齊文字繪製
static void drawSpacedStringLeft(LGFX& gfx, const String& str, int32_t x, int32_t y, int32_t letter_spacing = 4) {
    std::vector<String> chars;
    const char* p = str.c_str();
    while (*p) {
        int len = 1;
        unsigned char c = (unsigned char)*p;
        if ((c & 0x80) == 0) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        chars.push_back(String(p).substring(0, len));
        p += len;
    }
    int32_t cur_x = x;
    textdatum_t old_datum = gfx.getTextDatum();
    gfx.setTextDatum(textdatum_t::top_left);
    for (size_t i = 0; i < chars.size(); ++i) {
        gfx.drawString(chars[i], cur_x, y);
        cur_x += gfx.textWidth(chars[i].c_str()) + letter_spacing;
    }
    gfx.setTextDatum(old_datum);
}

void drawDashboardStatic() {
    uint16_t bg = lcd.color565(12, 16, 24);
    uint16_t header_bg = lcd.color565(18, 24, 38);
    uint16_t card_bg = lcd.color565(20, 28, 44);
    uint16_t card_border = lcd.color565(35, 48, 70);

    lcd.startWrite();
    // Full screen background
    lcd.fillScreen(bg);

    // TOP HEADER
    lcd.fillRect(0, 0, 800, 50, header_bg);
    lcd.setFont(&fonts::efontTW_16);
    lcd.setTextSize(1);
    lcd.setTextPadding(0);
    lcd.setTextColor(TFT_CYAN, header_bg);
    lcd.setCursor(20, 16);
    lcd.print("SnapMatrix 智慧面板 v1.3");

    // BOTTOM METRICS CARDS (Y=280 to 460)
    int cardY = 280;
    int cardH = 180;

    // Card 1: Status & Progress (X=16, W=240)
    lcd.fillRoundRect(16, cardY, 240, cardH, 12, card_bg);
    lcd.drawRoundRect(16, cardY, 240, cardH, 12, card_border);
    lcd.drawLine(24, cardY + 38, 248, cardY + 38, card_border);
    lcd.setFont(&fonts::efontTW_24_b);
    lcd.setTextSize(1);
    lcd.setTextPadding(0);
    lcd.setTextColor(lcd.color565(56, 189, 248), card_bg);
    drawSpacedStringLeft(lcd, "列印進度", 28, cardY + 10, 4);

    // Card 2: Temperatures (X=272, W=240)
    lcd.fillRoundRect(272, cardY, 240, cardH, 12, card_bg);
    lcd.drawRoundRect(272, cardY, 240, cardH, 12, card_border);
    lcd.drawLine(280, cardY + 38, 504, cardY + 38, card_border);
    lcd.setFont(&fonts::efontTW_24_b);
    lcd.setTextSize(1);
    lcd.setTextPadding(0);
    lcd.setTextColor(lcd.color565(56, 189, 248), card_bg);
    drawSpacedStringLeft(lcd, "溫度監控", 284, cardY + 10, 4);

    // Card 3: Print Job & Extruders (X=528, W=256)
    lcd.fillRoundRect(528, cardY, 256, cardH, 12, card_bg);
    lcd.drawRoundRect(528, cardY, 256, cardH, 12, card_border);
    lcd.drawLine(536, cardY + 38, 776, cardY + 38, card_border);
    lcd.setFont(&fonts::efontTW_24_b);
    lcd.setTextSize(1);
    lcd.setTextPadding(0);
    lcd.setTextColor(lcd.color565(56, 189, 248), card_bg);
    drawSpacedStringLeft(lcd, printer_model == "ideaformer_ir3" ? "任務與進度" : "任務與層數", 540, cardY + 10, 4);

    // Initial Matrix Render
    matrix.renderToLGFX(&lcd, 16, 65, 24, 9, true);

    lcd.endWrite();
    dashboard_initialized = true;
}

void drawWeatherIconLarge(int cx, int cy, int code) {
    if (code >= 95) {
        // Thunderstorm (雷雨)
        // Dark Thunder Cloud
        lcd.fillSmoothRoundRect(cx - 24, cy - 10, 50, 18, 9, lcd.color565(60, 75, 100));
        lcd.fillSmoothCircle(cx - 8, cy - 12, 15, lcd.color565(60, 75, 100));
        lcd.fillSmoothRoundRect(cx - 26, cy + 2, 54, 20, 10, lcd.color565(130, 150, 180));
        lcd.fillSmoothCircle(cx - 12, cy + 2, 15, lcd.color565(130, 150, 180));
        lcd.fillSmoothCircle(cx + 4, cy - 5, 18, lcd.color565(160, 180, 210));
        lcd.fillSmoothCircle(cx + 18, cy + 4, 13, lcd.color565(130, 150, 180));
        // Raindrops
        uint16_t rain_col = lcd.color565(34, 211, 238);
        lcd.fillSmoothRoundRect(cx - 18, cy + 24, 4, 12, 2, rain_col);
        lcd.fillSmoothRoundRect(cx + 16, cy + 24, 4, 12, 2, rain_col);
        // Smooth Lightning Bolt ⚡
        uint16_t bolt = lcd.color565(255, 230, 0);
        lcd.fillTriangle(cx - 4, cy + 12, cx + 10, cy + 12, cx - 2, cy + 24, bolt);
        lcd.fillTriangle(cx + 1, cy + 22, cx + 9, cy + 22, cx - 6, cy + 40, bolt);
    } else if (code >= 51 && code <= 82) {
        // Rain (下雨 / 毛毛雨 / 陣雨) - Layered Smooth Clouds + Raindrops
        // Background shadow cloud
        lcd.fillSmoothRoundRect(cx - 22, cy - 10, 48, 18, 9, lcd.color565(75, 100, 135));
        lcd.fillSmoothCircle(cx - 6, cy - 12, 15, lcd.color565(75, 100, 135));
        lcd.fillSmoothCircle(cx + 12, cy - 9, 13, lcd.color565(75, 100, 135));
        // Foreground soft cloud
        lcd.fillSmoothRoundRect(cx - 26, cy + 2, 54, 20, 10, lcd.color565(175, 205, 240));
        lcd.fillSmoothCircle(cx - 12, cy + 2, 15, lcd.color565(175, 205, 240));
        lcd.fillSmoothCircle(cx + 4, cy - 5, 18, lcd.color565(220, 238, 255));
        lcd.fillSmoothCircle(cx + 18, cy + 4, 13, lcd.color565(175, 205, 240));
        // 3 Capsule-shaped Raindrops
        uint16_t rain_col = lcd.color565(34, 211, 238);
        lcd.fillSmoothRoundRect(cx - 15, cy + 24, 4, 13, 2, rain_col);
        lcd.fillSmoothRoundRect(cx, cy + 27, 4, 15, 2, rain_col);
        lcd.fillSmoothRoundRect(cx + 15, cy + 23, 4, 13, 2, rain_col);
    } else if (code <= 1) {
        // Sun (晴天) - Warm glowing sun with smooth radiating corona dots
        lcd.fillSmoothCircle(cx, cy, 26, lcd.color565(255, 160, 0));
        lcd.fillSmoothCircle(cx, cy, 20, lcd.color565(255, 225, 40));
        // Smooth circular corona beacons
        for (int i = 0; i < 8; i++) {
            float a = i * (3.14159265f / 4.0f);
            int sx = cx + (int)round(cos(a) * 31);
            int sy = cy + (int)round(sin(a) * 31);
            lcd.fillSmoothCircle(sx, sy, 3, lcd.color565(255, 205, 30));
        }
    } else {
        // Cloud (多雲 / 陰天) - 3D Layered Smooth Fluffy Clouds
        // Back cloud
        lcd.fillSmoothRoundRect(cx - 20, cy - 10, 48, 20, 10, lcd.color565(90, 115, 150));
        lcd.fillSmoothCircle(cx - 6, cy - 12, 16, lcd.color565(90, 115, 150));
        lcd.fillSmoothCircle(cx + 12, cy - 10, 14, lcd.color565(90, 115, 150));
        // Front cloud
        lcd.fillSmoothRoundRect(cx - 26, cy + 4, 54, 22, 11, lcd.color565(190, 215, 245));
        lcd.fillSmoothCircle(cx - 12, cy + 4, 16, lcd.color565(190, 215, 245));
        lcd.fillSmoothCircle(cx + 4, cy - 3, 19, lcd.color565(235, 245, 255));
        lcd.fillSmoothCircle(cx + 18, cy + 6, 13, lcd.color565(190, 215, 245));
    }
}

// 輔助函數：以微像素多重對稱膨脹繪製，使向量字體筆劃厚度顯著加粗，呈現 ExtraBold / Heavy 粗壯質感
static void drawHeavyString(LGFX& gfx, const String& str, int32_t x, int32_t y, uint16_t fg_color, uint16_t bg_color, int stroke_extra = 1) {
    gfx.setTextColor(fg_color, bg_color);
    for (int dx = -stroke_extra; dx <= stroke_extra; ++dx) {
        for (int dy = -stroke_extra; dy <= stroke_extra; ++dy) {
            if (dx == 0 && dy == 0) continue;
            gfx.drawString(str, x + dx, y + dy);
        }
    }
    gfx.drawString(str, x, y);
}

void renderStandbyUI(const PrinterState& pState) {
    static int last_weather_temp = -999;
    static int last_weather_humidity = -999;
    static int last_weather_code = -999;
    static String last_weather_city = "";
    static int last_day = -1;

    if (!standby_initialized) {
        lcd.startWrite();
        // Clear background underneath matrix
        lcd.fillRect(0, 260, 800, 220, lcd.color565(12, 16, 24));
        
        // Large Weather Card (Aligned at Y=275, H=185)
        uint16_t card_bg = lcd.color565(20, 28, 44);
        uint16_t border_col = lcd.color565(40, 55, 80);
        lcd.fillRoundRect(24, 275, 752, 185, 14, card_bg);
        lcd.drawRoundRect(24, 275, 752, 185, 14, border_col);

        // Column Dividers
        lcd.drawLine(275, 290, 275, 445, lcd.color565(32, 45, 68));
        lcd.drawLine(525, 290, 525, 445, lcd.color565(32, 45, 68));

        // Column Titles (Enlarged and bold titles with Tracking/Letter-Spacing to prevent crowding)
        lcd.setFont(&fonts::efontTW_24_b);
        lcd.setTextSize(1.25);
        lcd.setTextPadding(0);
        lcd.setTextColor(lcd.color565(56, 189, 248), card_bg);
        drawSpacedStringCenter(lcd, "今日天氣狀況", 150, 298, 6);
        drawSpacedStringCenter(lcd, "即時環境氣溫", 400, 298, 6);
        drawSpacedStringCenter(lcd, "即時相對濕度", 650, 298, 6);
        lcd.setTextSize(1);
        lcd.setTextDatum(textdatum_t::top_left);

        standby_initialized = true;
        last_weather_temp = -999;
        last_weather_humidity = -999;
        last_weather_code = -999;
        last_weather_city = "";
        last_day = -1;
        lcd.endWrite();
    }

    // Render Date (below Temperature) & Weekday (below Humidity)
    time_t now;
    time(&now);
    struct tm * timeinfo = localtime(&now);
    if (timeinfo && timeinfo->tm_year > (2020 - 1900)) {
        if (timeinfo->tm_yday != last_day) {
            last_day = timeinfo->tm_yday;
            lcd.startWrite();
            
            // Clear date (center col) & weekday (right col) areas (Y=405 to 452)
            lcd.fillRect(280, 405, 240, 46, lcd.color565(20, 28, 44));
            lcd.fillRect(530, 405, 240, 46, lcd.color565(20, 28, 44));
            
            char date_buf[32];
            snprintf(date_buf, sizeof(date_buf), "%d年%d月%d日", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
            
            const char* const wdays[] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};
            const char* wday_str = wdays[timeinfo->tm_wday % 7];

            lcd.setFont(&fonts::efontTW_24_b);
            lcd.setTextSize(1);
            lcd.setTextPadding(0);
            lcd.setTextColor(lcd.color565(170, 200, 235), lcd.color565(20, 28, 44));
            
            lcd.setTextDatum(textdatum_t::middle_center);
            lcd.drawString(date_buf, 400, 428);
            lcd.drawString(wday_str, 650, 428);
            lcd.setTextDatum(textdatum_t::top_left);
            
            lcd.endWrite();
        }
    }

    if (weatherService.data.valid) {
        if (weatherService.data.code != last_weather_code || weatherService.data.city != last_weather_city) {
            last_weather_code = weatherService.data.code;
            last_weather_city = weatherService.data.city;
            lcd.startWrite();
            // Left column: weather icon and condition
            lcd.fillRect(26, 315, 246, 140, lcd.color565(20, 28, 44));
            
            drawWeatherIconLarge(150, 355, last_weather_code);

            lcd.setFont(&fonts::efontTW_24_b);
            lcd.setTextSize(1);
            lcd.setTextPadding(0);
            lcd.setTextColor(TFT_CYAN, lcd.color565(20, 28, 44));
            lcd.setTextDatum(textdatum_t::middle_center);
            String weather_str = last_weather_city + " " + weatherService.data.desc_cn;
            lcd.drawString(weather_str, 150, 428);
            lcd.setTextDatum(textdatum_t::top_left);
            lcd.endWrite();
        }

        if ((int)round(weatherService.data.temp) != last_weather_temp) {
            last_weather_temp = (int)round(weatherService.data.temp);
            lcd.startWrite();
            // Center column: temperature
            lcd.fillRect(280, 325, 240, 80, lcd.color565(20, 28, 44));
            
            // Extra Large & Heavy Bold Vector Number (FreeSansBold24pt7b @ 1.7x with Multi-pixel Heavy Overdraw)
            lcd.setFont(&fonts::FreeSansBold24pt7b);
            lcd.setTextSize(1.7);
            lcd.setTextPadding(0);
            lcd.setTextDatum(textdatum_t::middle_right);
            // 數字往右平移至 432，讓數字主體完美居中於 X=400 欄位中心，筆劃加粗膨脹 (Heavy)
            drawHeavyString(lcd, String(last_weather_temp), 432, 370, (uint16_t)lcd.color565(255, 155, 45), (uint16_t)lcd.color565(20, 28, 44), 1);
            
            // High-Resolution Unit Symbol (°C)
            lcd.setFont(&fonts::efontTW_24_b);
            lcd.setTextSize(1.2);
            lcd.setTextDatum(textdatum_t::middle_left);
            lcd.setTextColor(lcd.color565(255, 190, 110), lcd.color565(20, 28, 44));
            lcd.drawString("°C", 440, 365);
            lcd.setTextSize(1);
            lcd.setTextDatum(textdatum_t::top_left);
            lcd.endWrite();
        }

        if (weatherService.data.humidity != last_weather_humidity) {
            last_weather_humidity = weatherService.data.humidity;
            lcd.startWrite();
            // Right column: humidity
            lcd.fillRect(530, 325, 240, 80, lcd.color565(20, 28, 44));
            
            // Extra Large & Heavy Vibrant Pink Vector Number (亮麗櫻花粉紅色 - 真正的粉紅！)
            lcd.setFont(&fonts::FreeSansBold24pt7b);
            lcd.setTextSize(1.7);
            lcd.setTextPadding(0);
            lcd.setTextDatum(textdatum_t::middle_right);
            // 數字往右平移至 682，讓數字主體完美居中於 X=650 欄位中心，精確傳遞 uint16_t RGB565
            drawHeavyString(lcd, String(last_weather_humidity), 682, 370, (uint16_t)lcd.color565(255, 90, 160), (uint16_t)lcd.color565(20, 28, 44), 1);
            
            // High-Resolution Unit Symbol (%) - 嚴格保持原版經典天空藍色
            lcd.setFont(&fonts::FreeSansBold18pt7b);
            lcd.setTextSize(1.1);
            lcd.setTextDatum(textdatum_t::middle_left);
            lcd.setTextColor((uint16_t)lcd.color565(60, 190, 255), (uint16_t)lcd.color565(20, 28, 44));
            lcd.drawString("%", 690, 365);
            lcd.setTextSize(1);
            lcd.setTextDatum(textdatum_t::top_left);
            lcd.endWrite();
        }
    }

    // Bottom Status & Touch Hint Bar (Y = 462~478, perfectly fills bottom space)
    static String last_bottom_status = "";
    String bottom_status = "";
    if (pState.is_connected) {
        if (pState.gcode_state == "RUNNING" || pState.gcode_state == "PAUSE") {
            bottom_status = "列印中 " + String(pState.mc_percent) + "%  ·  噴頭 " + String(pState.nozzle_temper) + "°C  ·  輕觸螢幕或按鍵切換儀表板";
        } else {
            bottom_status = "印表機待命中  ·  噴頭 " + String(pState.nozzle_temper) + "°C / 熱床 " + String(pState.bed_temper) + "°C  ·  輕觸螢幕或按鍵切換儀表板";
        }
    } else {
        bottom_status = "印表機未連線  ·  輕觸螢幕或按鍵切換儀表板";
    }

    if (!standby_initialized || bottom_status != last_bottom_status) {
        last_bottom_status = bottom_status;
        lcd.startWrite();
        lcd.fillRect(10, 461, 780, 18, lcd.color565(12, 16, 24));
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(pState.is_connected ? lcd.color565(110, 150, 190) : lcd.color565(220, 100, 100), lcd.color565(12, 16, 24));
        lcd.setTextDatum(textdatum_t::middle_center);
        lcd.drawString(bottom_status, 400, 470);
        lcd.setTextDatum(textdatum_t::top_left);
        lcd.endWrite();
    }
}

void renderDashboardUI(const PrinterState& pState) {
    static bool first_run = true;
    static bool last_connected = false;
    static String last_printer_ip = "";
    static String last_device_ip = "";
    
    // Card 1 cache
    static int last_mc_percent = -1;
    static String last_gcode_state = "";
    static int last_remaining_time = -999;
    static String last_ota_url = "";
    
    // Card 2 cache
    static int last_nozzle_temper = -999;
    static int last_nozzle_target = -999;
    static int last_bed_temper = -999;
    static int last_bed_target = -999;
    static int last_chamber_temper = -999;
    static int last_host_temper = -999;
    static int last_flow_rate = -999;
    
    // Card 3 cache
    static int last_layer_num = -1;
    static int last_total_layer_num = -1;
    static float last_filament_used_m = -1.0f;
    static String last_fil_name = "INIT_UNSET";
    static int last_speed_pct = -1;
    static int last_card3_flow = -1;
    static String last_gcode_file = "INIT_UNSET";
    static String last_t_state[4] = {"", "", "", ""};
    static uint32_t last_filament_rgb[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    static bool last_filament_loaded[4] = {false, false, false, false};

    // Detect printer model switch and reset all caches
    static String last_rendered_profile = "";
    if (last_rendered_profile != printer_model) {
        last_rendered_profile = printer_model;
        first_run = true;
        dashboard_initialized = false;
        last_connected = !pState.is_connected;
        last_printer_ip = "";
        last_device_ip = "";
        last_mc_percent = -1;
        last_gcode_state = "";
        last_remaining_time = -999;
        last_ota_url = "";
        last_nozzle_temper = -999;
        last_nozzle_target = -999;
        last_bed_temper = -999;
        last_bed_target = -999;
        last_chamber_temper = -999;
        last_host_temper = -999;
        last_flow_rate = -999;
        last_layer_num = -1;
        last_total_layer_num = -1;
        last_filament_used_m = -1.0f;
        last_fil_name = "RESET";
        last_speed_pct = -1;
        last_card3_flow = -1;
        last_gcode_file = "RESET";
        for (int i = 0; i < 4; i++) {
            last_t_state[i] = "RESET";
            last_filament_rgb[i] = 0;
            last_filament_loaded[i] = false;
        }
    }

    if (in_screensaver) {
        // 1. Render Virtual Matrix (Static 24h Clock)
        lcd.startWrite();
        matrix.renderToLGFX(&lcd, 16, 65, 24, 9, false);
        lcd.endWrite();

        // 2. Render Full High-Def Weather & Humidity Dashboard underneath
        renderStandbyUI(pState);
        return;
    }

    if (!dashboard_initialized) {
        drawDashboardStatic();
        first_run = true;
    }

    lcd.startWrite();

    // 1. Render Virtual Matrix (Differential, ultra-fast)
    matrix.renderToLGFX(&lcd, 16, 65, 24, 9, false);

    // 2. Differential Cards & Header update (Only render when values actually change!)
    uint16_t header_bg = lcd.color565(18, 24, 38);
    uint16_t card_bg = lcd.color565(20, 28, 44);
    int cardY = 280;

    // Header: Online state
    if (first_run || pState.is_connected != last_connected) {
        last_connected = pState.is_connected;
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(90);
        lcd.setTextColor(pState.is_connected ? TFT_GREEN : TFT_RED, header_bg);
        lcd.setCursor(680, 16);
        lcd.print(pState.is_connected ? "[已連線]" : "[未連線]");
    }

    // Header: Printer IP & ROM Version
    static String last_printer_info = "";
    String cur_printer_info = (pState.rom_version.length() > 0) ? 
        ("印表機: " + moonraker.printer_ip + " (" + pState.rom_version + ")") : 
        ("印表機: " + moonraker.printer_ip);

    if (first_run || cur_printer_info != last_printer_info) {
        last_printer_info = cur_printer_info;
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(260);
        lcd.setTextColor(TFT_LIGHTGRAY, header_bg);
        lcd.setCursor(215, 16);
        lcd.print(last_printer_info);
    }

    // Header: Device IP
    String cur_device_ip = WiFi.localIP().toString();
    if (first_run || cur_device_ip != last_device_ip) {
        last_device_ip = cur_device_ip;
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(180);
        lcd.setTextColor(TFT_LIGHTGRAY, header_bg);
        lcd.setCursor(485, 16);
        lcd.printf("本機: %s", last_device_ip.c_str());
        lcd.setTextPadding(0);
    }

    // Serial Logging (Throttled every 3 seconds)
    static unsigned long last_serial_log = 0;
    if (millis() - last_serial_log >= 3000) {
        last_serial_log = millis();
        Serial.printf("[Dashboard] %s | 狀態: %s | 進度: %d%% | 流量: %d%% | 噴頭: %d/%d C | 熱床: %d/%d C | 層數: %d/%d\n",
                      pState.is_connected ? "已連線" : "未連線",
                      get_status_chinese(pState.gcode_state).c_str(),
                      pState.mc_percent,
                      (int)round(pState.extrude_factor * 100.0f),
                      pState.nozzle_temper, pState.nozzle_target_temper,
                      pState.bed_temper, pState.bed_target_temper,
                      pState.layer_num, pState.total_layer_num);
    }

    // CARD 1: Progress & Status
    // Row 1: Print Progress Percentage (Right-aligned, vivid yellow)
    if (first_run || pState.mc_percent != last_mc_percent) {
        last_mc_percent = pState.mc_percent;
        lcd.fillRect(25, cardY + 39, 222, 35, card_bg);
        lcd.setFont(&fonts::FreeSansBold24pt7b);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextDatum(textdatum_t::middle_right);
        lcd.setTextColor(lcd.color565(255, 235, 0), card_bg); // 鮮豔明亮高飽和黃色
        lcd.drawString(String(last_mc_percent) + "%", 244, cardY + 51);
        lcd.setTextDatum(textdatum_t::top_left);
    }

    // Row 2: 狀態 (At original position cardY + 76)
    if (first_run || pState.gcode_state != last_gcode_state) {
        last_gcode_state = pState.gcode_state;
        lcd.fillRect(25, cardY + 74, 220, 28, card_bg);
        lcd.setFont(&fonts::efontTW_24_b);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setCursor(28, cardY + 76);
        lcd.setTextColor(lcd.color565(148, 163, 184), card_bg);
        lcd.print("狀態: ");
        uint16_t stat_col = TFT_GREEN;
        if (last_gcode_state == "PAUSE") stat_col = TFT_ORANGE;
        else if (last_gcode_state == "FAILED") stat_col = TFT_RED;
        else if (last_gcode_state == "RUNNING") stat_col = TFT_CYAN;
        lcd.setTextColor(stat_col, card_bg);
        lcd.print(get_status_chinese(last_gcode_state));
    }

    // Row 3: 剩餘 / 天氣 (At original position cardY + 108)
    static bool last_weather_valid = false;
    static int last_weather_code = -1;
    static int last_weather_temp = -999;
    if (first_run || pState.mc_remaining_time != last_remaining_time || pState.gcode_state != last_gcode_state || 
        weatherService.data.valid != last_weather_valid || weatherService.data.code != last_weather_code || (int)weatherService.data.temp != last_weather_temp) {
        last_remaining_time = pState.mc_remaining_time;
        last_weather_valid = weatherService.data.valid;
        last_weather_code = weatherService.data.code;
        last_weather_temp = (int)weatherService.data.temp;
        lcd.fillRect(25, cardY + 106, 220, 28, card_bg);
        lcd.setFont(&fonts::efontTW_24_b);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setCursor(28, cardY + 108);
        lcd.setTextColor(lcd.color565(148, 163, 184), card_bg);
        if (pState.gcode_state == "IDLE" && weatherService.data.valid) {
            lcd.print("天氣: ");
            lcd.setTextColor(TFT_WHITE, card_bg);
            lcd.printf("%s %d°C", weatherService.data.city.c_str(), (int)round(weatherService.data.temp));
        } else {
            lcd.print("剩餘: ");
            lcd.setTextColor(TFT_WHITE, card_bg);
            lcd.print(last_remaining_time >= 0 ? format_time(last_remaining_time) : "--");
        }
    }

    // Row 4: OTA (At original position cardY + 142)
    String cur_ota_url = "OTA: " + cur_device_ip;
    if (first_run || cur_ota_url != last_ota_url) {
        last_ota_url = cur_ota_url;
        lcd.fillRect(25, cardY + 138, 220, 24, card_bg);
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(lcd.color565(120, 145, 175), card_bg);
        lcd.setCursor(28, cardY + 142);
        lcd.print(last_ota_url);
    }

    // CARD 2: Temperatures
    if (first_run || pState.nozzle_temper != last_nozzle_temper || pState.nozzle_target_temper != last_nozzle_target) {
        last_nozzle_temper = pState.nozzle_temper;
        last_nozzle_target = pState.nozzle_target_temper;
        lcd.fillRect(280, cardY + 46, 225, 30, card_bg);
        lcd.setFont(&fonts::efontTW_24_b);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setCursor(284, cardY + 48);
        lcd.setTextColor(lcd.color565(148, 163, 184), card_bg);
        lcd.print("噴頭: ");
        lcd.setTextColor(lcd.color565(255, 120, 120), card_bg);
        lcd.printf("%d / %d °C", last_nozzle_temper, last_nozzle_target);
    }

    if (first_run || pState.bed_temper != last_bed_temper || pState.bed_target_temper != last_bed_target) {
        last_bed_temper = pState.bed_temper;
        last_bed_target = pState.bed_target_temper;
        lcd.fillRect(280, cardY + 83, 225, 30, card_bg);
        lcd.setFont(&fonts::efontTW_24_b);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setCursor(284, cardY + 85);
        lcd.setTextColor(lcd.color565(148, 163, 184), card_bg);
        lcd.print("熱床: ");
        lcd.setTextColor(lcd.color565(100, 180, 255), card_bg);
        lcd.printf("%d / %d °C", last_bed_temper, last_bed_target);
    }

    // Card 2 Row 3: Isolated Chamber vs Host/Flow
    bool card2_row3_changed = false;
    if (printer_model == "ideaformer_ir3") {
        int cur_flow = (int)round(pState.extrude_factor * 100.0f);
        card2_row3_changed = (pState.host_temper != last_host_temper || cur_flow != last_flow_rate);
    } else {
        card2_row3_changed = (pState.chamber_temper != last_chamber_temper);
    }

    if (first_run || card2_row3_changed) {
        last_chamber_temper = pState.chamber_temper;
        last_host_temper = pState.host_temper;
        last_flow_rate = (int)round(pState.extrude_factor * 100.0f);
        lcd.fillRect(280, cardY + 120, 225, 30, card_bg);
        lcd.setFont(&fonts::efontTW_24_b);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setCursor(284, cardY + 122);
        lcd.setTextColor(lcd.color565(148, 163, 184), card_bg);
        if (printer_model == "ideaformer_ir3") {
            if (pState.host_temper > 0) {
                lcd.print("主機: ");
                lcd.setTextColor(lcd.color565(140, 230, 160), card_bg);
                lcd.printf("%d °C", pState.host_temper);
            } else {
                lcd.print("流量: ");
                lcd.setTextColor(lcd.color565(140, 230, 160), card_bg);
                lcd.printf("%d%%", last_flow_rate);
            }
        } else {
            lcd.print("機箱: ");
            lcd.setTextColor(lcd.color565(140, 230, 160), card_bg);
            lcd.printf("%d °C", last_chamber_temper);
        }
    }

    // CARD 3: Layers & Extruders (Isolated for Ideaformer IR3 Belt Printer)
    bool card3_row1_changed = false;
    if (printer_model == "ideaformer_ir3") {
        if (pState.total_layer_num > 0) {
            card3_row1_changed = (pState.layer_num != last_layer_num || pState.total_layer_num != last_total_layer_num);
        } else {
            card3_row1_changed = (pState.filament_used_m != last_filament_used_m);
        }
    } else {
        card3_row1_changed = (pState.layer_num != last_layer_num || pState.total_layer_num != last_total_layer_num);
    }

    if (first_run || card3_row1_changed) {
        last_layer_num = pState.layer_num;
        last_total_layer_num = pState.total_layer_num;
        last_filament_used_m = pState.filament_used_m;
        lcd.fillRect(535, cardY + 46, 240, 30, card_bg);
        lcd.setFont(&fonts::efontTW_24_b);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setCursor(540, cardY + 48);
        lcd.setTextColor(lcd.color565(148, 163, 184), card_bg);
        if (printer_model == "ideaformer_ir3") {
            if (last_total_layer_num > 0) {
                lcd.print("層數: ");
                lcd.setTextColor(lcd.color565(250, 204, 21), card_bg);
                lcd.printf("%d / %d", last_layer_num, last_total_layer_num);
            } else {
                lcd.print("耗材: ");
                lcd.setTextColor(lcd.color565(250, 204, 21), card_bg);
                lcd.printf("%.1f m", last_filament_used_m);
            }
        } else {
            lcd.print("層數: ");
            lcd.setTextColor(lcd.color565(250, 204, 21), card_bg);
            lcd.printf("%d / %d", last_layer_num, last_total_layer_num);
        }
    }

    if (first_run || pState.gcode_file != last_gcode_file) {
        last_gcode_file = pState.gcode_file;
        lcd.fillRect(535, cardY + 83, 240, 30, card_bg);
        lcd.setFont(&fonts::efontTW_24_b);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setCursor(540, cardY + 85);
        lcd.setTextColor(lcd.color565(148, 163, 184), card_bg);
        lcd.print("檔案: ");
        lcd.setTextColor(TFT_WHITE, card_bg);
        String fname = last_gcode_file;
        if (fname.endsWith(".gcode")) fname = fname.substring(0, fname.length() - 6);
        if (fname.length() > 14) fname = fname.substring(0, 11) + "..";
        lcd.print(fname.length() > 0 ? fname : "無任務");
    }

    // Bottom of Card 3: Filament / Machine specific details
    if (printer_model == "ideaformer_ir3") {
        int cur_speed_pct = (int)round(pState.speed_factor * 100.0f);
        int cur_flow_pct = (int)round(pState.extrude_factor * 100.0f);

        if (first_run || pState.filament_used_m != last_filament_used_m || pState.filament_name != last_fil_name ||
            cur_speed_pct != last_speed_pct || cur_flow_pct != last_card3_flow) {
            last_filament_used_m = pState.filament_used_m;
            last_fil_name = pState.filament_name;
            last_speed_pct = cur_speed_pct;
            last_card3_flow = cur_flow_pct;

            lcd.fillRect(535, cardY + 115, 240, 55, card_bg);
            lcd.setFont(&fonts::efontTW_16);
            lcd.setTextSize(1);
            lcd.setTextPadding(0);

            // Row 1: Filament Name & Usage
            lcd.setCursor(542, cardY + 120);
            lcd.setTextColor(lcd.color565(180, 160, 255), card_bg);
            String fName = (last_fil_name.length() > 0 && last_fil_name != "RESET") ? last_fil_name : "通用耗材";
            if (fName.length() > 12) fName = fName.substring(0, 11) + "..";
            lcd.printf("[%s] %.1fm", fName.c_str(), last_filament_used_m);

            // Row 2: Speed & Flow rates
            lcd.setCursor(542, cardY + 146);
            lcd.setTextColor(lcd.color565(56, 189, 248), card_bg);
            lcd.printf("速度: %d%% | 流量: %d%%", last_speed_pct, last_card3_flow);
        }
    } else {
        // Standard Snapmaker U1: 4 Slots Indicators (T1 ~ T4 Real Filament Colors - Enlarged Display)
        if (first_run) {
            lcd.fillRect(535, cardY + 118, 245, 55, card_bg);
        }
        for (int i = 0; i < 4; i++) {
            if (first_run || pState.t_state[i] != last_t_state[i] || pState.filament_rgb[i] != last_filament_rgb[i] || pState.filament_loaded[i] != last_filament_loaded[i]) {
                last_t_state[i] = pState.t_state[i];
                last_filament_rgb[i] = pState.filament_rgb[i];
                last_filament_loaded[i] = pState.filament_loaded[i];

                int cx = 562 + i * 60;
                int cy = cardY + 141;
                bool loaded = pState.filament_loaded[i] && (pState.t_state[i].length() == 0 || pState.t_state[i] != "wait_insert");
                
                uint8_t r = (pState.filament_rgb[i] >> 16) & 0xFF;
                uint8_t g = (pState.filament_rgb[i] >> 8) & 0xFF;
                uint8_t b = pState.filament_rgb[i] & 0xFF;
#if defined(BOARD_CYD_50)
                // Standard RGB565 color mapping for Sunton 5.0"
                uint16_t fill_color = loaded ? lcd.color565(r, g, b) : lcd.color565(55, 42, 35);
#else
                // Hardware RGB Pin mapping: pass (b, g, r) to match Sunton 7" LCD bus wiring
                uint16_t fill_color = loaded ? lcd.color565(b, g, r) : lcd.color565(55, 42, 35);
#endif
                
                // Calculate brightness to ensure text contrast
                int brightness = (r * 299 + g * 587 + b * 114) / 1000;
                
                // Draw enlarged filled colored circle (Radius = 17, Diameter = 34px)
                lcd.fillCircle(cx, cy, 17, fill_color);
                // Outer crisp border so dark/black filaments stand out
                lcd.drawCircle(cx, cy, 17, loaded ? lcd.color565(190, 210, 240) : lcd.color565(65, 75, 95));

                // Choose text color (White for dark colors, Black for bright colors)
                uint16_t text_col = (!loaded) ? lcd.color565(90, 100, 120) : (brightness > 135 ? TFT_BLACK : TFT_WHITE);

                // Bold centered text (Shifted X by +2px for perfect geometric centering)
                lcd.setFont(&fonts::Font0);
                lcd.setTextSize(2);
                lcd.setTextPadding(0);
                lcd.setTextDatum(textdatum_t::middle_center);
                lcd.setTextColor(text_col, fill_color);
                lcd.drawString("T" + String(i + 1), cx + 2, cy);
                
                // Reset text attributes
                lcd.setTextDatum(textdatum_t::top_left);
                lcd.setTextSize(1);
            }
        }
    }

    first_run = false;
    lcd.endWrite();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[系統] SnapMatrix CYD 7吋面板啟動中...");

    // Stable Wi-Fi Tx Power
    WiFi.setTxPower(WIFI_POWER_15dBm);

    // Initialize Hardware Button (GPIO 0 / BOOT Button for Non-Touch CYD boards)
    pinMode(0, INPUT_PULLUP);

    // Initialize LCD
    lcd.init();
    lcd.setRotation(0);
    lcd.setBrightness(BRIGHTNESS_NORMAL);
    lcd.fillScreen(TFT_BLACK);

    lcd.setFont(&fonts::efontTW_24_b);
    lcd.setTextDatum(textdatum_t::middle_center);

    // 原生高品質粗體標題 (36px)
    lcd.setTextSize(1.5);
    lcd.setTextColor(TFT_CYAN);
    lcd.drawString("SnapMatrix 智慧面板 v1.3", 400, 180);

    // 原生高品質粗體連線中提示 (29px)
    lcd.setTextSize(1.2);
    lcd.setTextColor(TFT_YELLOW);
    lcd.drawString("正在連線 Wi-Fi 網路...", 400, 250);

    // 輔助說明 (24px 原生粗體)
    lcd.setTextSize(1.0);
    lcd.setTextColor(TFT_LIGHTGRAY);
    lcd.drawString("系統啟動中，請稍候...", 400, 310);

    // 恢復預設
    lcd.setTextSize(1);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.setFont(&fonts::efontTW_16);

    preferences.begin("snap_cyd", false);
    printer_model = preferences.getString("printer_model", "snapmaker_u1");
    sleep_timeout = preferences.getInt("sleep_timeout", 10);
    applyProfile(printer_model);
    Serial.printf("[Preferences] 開機載入機型: %s, 印表機IP: %s, 休眠: %d 分鐘, Tailscale: %s (裝置: %s, Subnet Router: %s)\n", 
                  printer_model.c_str(), printer_ip.c_str(), sleep_timeout, ts_enabled ? "啟用" : "關閉", ts_dev_name.c_str(), ts_subnet_router.c_str());

    // WiFiManager Setup
    WiFiManager wm;
    wm.setHostname("SnapMatrix-CYD7");

    // Enlarge and beautify font styles for WiFiManager Web Configuration Portal
    wm.setCustomHeadElement(
        "<style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Noto Sans TC','PingFang TC','Microsoft JhengHei','Segoe UI',sans-serif;font-size:19px!important;font-weight:500;background:#0f172a;color:#f8fafc;padding:16px;-webkit-font-smoothing:antialiased;}"
        "h1,h2,h3{font-size:28px!important;font-weight:800!important;color:#38bdf8!important;margin-bottom:16px!important;letter-spacing:0.5px;}"
        "button,input[type='submit'],input[type='button']{font-size:20px!important;padding:16px 20px!important;margin:12px 0!important;border-radius:12px!important;font-weight:700!important;cursor:pointer;font-family:inherit;}"
        "input[type='text'],input[type='password'],select{font-size:19px!important;font-weight:600;padding:14px 16px!important;margin:8px 0 20px 0!important;border-radius:10px!important;background:#1e293b!important;color:#fff!important;border:1px solid #334155!important;width:100%!important;box-sizing:border-box!important;font-family:inherit;}"
        "label{font-size:18px!important;font-weight:700!important;color:#94a3b8!important;display:block;margin-top:10px;}"
        "a{font-size:18px!important;font-weight:600;padding:12px 16px!important;display:inline-block;color:#38bdf8;text-decoration:none;}"
        ".msg{font-size:18px!important;font-weight:600;padding:14px!important;border-radius:10px!important;margin:12px 0!important;}"
        "div{font-size:18px!important;}"
        "</style>"
    );

    WiFiManagerParameter custom_printer_ip("printer_ip", "Moonraker 印表機 IP", printer_ip.c_str(), 20);
    wm.addParameter(&custom_printer_ip);
    wm.setConfigPortalTimeout(180);

    // If no WiFi is saved (e.g. after erase flash), show clear bold instructions on LCD
    wm.setAPCallback([](WiFiManager *myWiFiManager) {
        lcd.fillScreen(TFT_BLACK);
        lcd.setFont(&fonts::efontTW_24_b);
        lcd.setTextDatum(textdatum_t::middle_center);
        
        // 標題加大 (原生 24px 粗體 x 1.8 = ~44px 超大粗體)
        lcd.setTextSize(1.8);
        lcd.setTextColor(TFT_CYAN);
        lcd.drawString("SnapMatrix 初始配網模式 v1.3", 400, 80);
        
        // 原生 24px 粗體各項指示
        lcd.setTextSize(1.0);
        lcd.setTextColor(TFT_WHITE);
        lcd.drawString("請用手機或電腦連線熱點：", 400, 160);
        
        lcd.setTextColor(TFT_YELLOW);
        char ssid_buf[64];
        snprintf(ssid_buf, sizeof(ssid_buf), "SSID: %s", myWiFiManager->getConfigPortalSSID().c_str());
        lcd.drawString(ssid_buf, 400, 220);
        
        lcd.setTextColor(TFT_GREEN);
        lcd.drawString("配網網址: http://192.168.4.1", 400, 280);
        
        lcd.setTextColor(TFT_LIGHTGRAY);
        lcd.drawString("輸入 Wi-Fi 密碼與印表機 IP 即可開機", 400, 350);

        lcd.setTextSize(1);
        lcd.setTextDatum(textdatum_t::top_left);
        lcd.setFont(&fonts::efontTW_16);
    });

    if (!wm.autoConnect("SnapMatrix-CYD7-Setup")) {
        Serial.println("[WiFi] 網路設定逾時，以離線模式開機...");
    } else {
        Serial.printf("[WiFi] 連線成功！IP: %s\n", WiFi.localIP().toString().c_str());
    }

    if (String(custom_printer_ip.getValue()).length() > 0) {
        printer_ip = String(custom_printer_ip.getValue());
        preferences.putString("printer_ip", printer_ip);
    }

    // Tailscale State Callback
    tailscaleService.onStateChange([](bool connected, const String &vpnIp) {
        if (connected) {
            Serial.printf("[Tailscale Event] VPN 連線就緒 (IP: %s)，嘗試重連印表機: %s\n", 
                          vpnIp.c_str(), printer_ip.c_str());
            moonraker.setPrinterIP(printer_ip);
        }
    });

    // Start Tailscale Client if enabled
    if (ts_enabled && ts_auth_key.length() > 0) {
        Serial.println("[Tailscale] 正在啟動 Tailscale VPN 客戶端服務...");
        tailscaleService.begin(true, ts_auth_key, ts_dev_name, printer_ip, ts_subnet_router);
    }

    // NTP Time Sync
    configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    // Start Weather Fetch
    weatherService.triggerUpdateAsync();

    // API endpoint for live status polling
    server.on("/api/status", HTTP_GET, []() {
        PrinterState pState;
        moonraker.getState(pState);
        String rom_info = "";
        if (pState.rom_version.length() > 0) {
            rom_info = pState.machine_model + " " + pState.rom_version;
        } else if (pState.is_connected) {
            rom_info = pState.machine_model + " (讀取中...)";
        } else {
            rom_info = "尚未連線至印表機";
        }
        String json = "{\"connected\":" + String(pState.is_connected ? "true" : "false") + 
                      ",\"rom\":\"" + rom_info + "\"" +
                      ",\"ts_enabled\":" + String(tailscaleService.isEnabled() ? "true" : "false") +
                      ",\"ts_connected\":" + String(tailscaleService.isConnected() ? "true" : "false") +
                      ",\"ts_ip\":\"" + tailscaleService.getVpnIp() + "\"" +
                      ",\"ts_status\":\"" + tailscaleService.getStateString() + "\"" +
                      ",\"ts_debug\":" + tailscaleService.getDebugJson() + "}";
        server.send(200, "application/json", json);
    });

    // ElegantOTA & Web Dashboard Setup
    server.on("/", HTTP_GET, []() {
        PrinterState pState;
        moonraker.getState(pState);
        if (pState.rom_version.length() == 0 && pState.is_connected) {
            moonraker.fetchSystemInfo();
            moonraker.getState(pState);
        }

        String weather_status = weatherService.data.valid ? 
            (weatherService.data.city + " " + String((int)round(weatherService.data.temp)) + "°C " + String(weatherService.data.humidity) + "% (" + weatherService.data.desc_cn + ")") : "取得中...";

        String rom_info = "";
        if (pState.rom_version.length() > 0) {
            rom_info = pState.machine_model + " " + pState.rom_version;
        } else if (pState.is_connected) {
            rom_info = pState.machine_model + " (讀取中...)";
        } else {
            rom_info = "尚未連線至印表機";
        }

        PrinterProfileConfig u1_cfg = getProfileConfig("snapmaker_u1");
        PrinterProfileConfig ir3_cfg = getProfileConfig("ideaformer_ir3");

        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<title>SnapMatrix 控制面板 v1.3</title><style>"
                      "body{font-family:-apple-system,BlinkMacSystemFont,'Noto Sans TC','PingFang TC','Microsoft JhengHei','Segoe UI',sans-serif;background:#0f172a;color:#f8fafc;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;padding:24px;box-sizing:border-box;-webkit-font-smoothing:antialiased;}"
                      ".card{background:rgba(30,41,59,0.85);backdrop-filter:blur(16px);border:1px solid rgba(255,255,255,0.15);padding:36px;border-radius:24px;width:100%;max-width:540px;box-shadow:0 24px 48px rgba(0,0,0,0.5);}"
                      "h2{margin-top:0;color:#38bdf8;font-size:28px;font-weight:800;display:flex;align-items:center;gap:12px;margin-bottom:24px;letter-spacing:0.5px;}"
                      "label{font-size:18px;font-weight:700;color:#94a3b8;margin-bottom:10px;display:block;}"
                      "input[type=text],input[type=number]{width:100%;padding:16px 18px;background:#1e293b;border:1px solid #334155;border-radius:12px;color:#fff;font-size:18px;font-weight:600;margin-bottom:24px;box-sizing:border-box;outline:none;transition:all 0.2s;font-family:inherit;}"
                      "input:focus{border-color:#38bdf8;box-shadow:0 0 0 3px rgba(56,189,248,0.25);}"
                      "button{width:100%;padding:16px;background:linear-gradient(135deg,#0284c7,#0369a1);color:#fff;border:none;border-radius:12px;font-size:20px;font-weight:800;cursor:pointer;transition:all 0.2s;box-shadow:0 4px 14px rgba(2,132,199,0.35);font-family:inherit;}"
                      "button:hover{opacity:0.95;transform:translateY(-1px);}"
                      ".btn-ota{margin-top:16px;background:#334155;text-align:center;text-decoration:none;display:block;padding:16px;border-radius:12px;color:#e2e8f0;font-size:18px;font-weight:700;transition:all 0.2s;font-family:inherit;}"
                      ".btn-ota:hover{background:#475569;color:#fff;}"
                      ".status{font-size:17px;font-weight:600;line-height:1.6;color:#10b981;margin-bottom:26px;background:rgba(16,185,129,0.12);border:1px solid rgba(16,185,129,0.25);padding:14px 18px;border-radius:12px;display:flex;flex-direction:column;gap:8px;}"
                      ".status-row{display:flex;justify-content:space-between;flex-wrap:wrap;gap:4px;}"
                      "</style></head><body><div class='card'>"
                      "<h2>SnapMatrix 控制面板 <span style='font-size:15px;background:rgba(56,189,248,0.18);border:1px solid rgba(56,189,248,0.3);color:#38bdf8;padding:3px 10px;border-radius:20px;font-weight:700;margin-left:auto;'>v1.3 正式版</span></h2>"
                      "<div class='status'>"
                      "<div class='status-row'><span>裝置 IP: " + WiFi.localIP().toString() + "</span><span id='conn-status'>狀態: " + (pState.is_connected ? "已連線" : "未連線") + "</span></div>"
                      "<div class='status-row'><span style='color:#94a3b8;'>韌體版本:</span><span style='font-weight:600;color:#38bdf8;'>v1.3</span></div>"
                      "<div class='status-row'><span style='color:#a78bfa;'>🖨️ 印表機 ROM:</span><span id='rom-text' style='font-weight:600;color:#fff;'>" + rom_info + "</span></div>"
                      "<div class='status-row'><span style='color:#38bdf8;'>⛅ 當前天氣:</span><span>" + weather_status + "</span></div>"
                      "<div class='status-row'><span style='color:#38bdf8;'>🌐 Tailscale:</span><span id='ts-status-text' style='font-weight:600;'>" + tailscaleService.getStateString() + "</span></div>"
                      "</div>"
                      "<form action='/save' method='POST'>"
                      "<label>選擇印表機機型 (Profile):</label>"
                      "<select id='printer_model' name='printer_model' onchange='onProfileChange(this.value)' style='width:100%;padding:16px 18px;background:#1e293b;border:1px solid #334155;border-radius:12px;color:#fff;font-size:18px;font-weight:600;margin-bottom:24px;box-sizing:border-box;outline:none;font-family:inherit;'>"
                      "<option value='snapmaker_u1'" + String(printer_model == "snapmaker_u1" ? " selected" : "") + ">Snapmaker U1 (4色/多耗材)</option>"
                      "<option value='ideaformer_ir3'" + String(printer_model == "ideaformer_ir3" ? " selected" : "") + ">Ideaformer IR3 V2 (輸送帶/標準Klipper)</option>"
                      "</select>"
                      "<label>Moonraker 印表機 IP 位址:</label>"
                      "<input type='text' id='printer_ip' name='printer_ip' value='" + printer_ip + "' oninput='onFieldInput()' placeholder='例如: 192.168.200.235 或 Tailscale IP 100.x.y.z' required>"
                      "<label>螢幕自動休眠時間 (分鐘，0 為不休眠):</label>"
                      "<input type='number' name='sleep' value='" + String(sleep_timeout) + "' min='0' max='120'>"
                      "<div style='margin:20px 0 26px 0;padding:18px;background:rgba(15,23,42,0.6);border:1px solid #334155;border-radius:14px;'>"
                      "<label style='display:flex;align-items:center;cursor:pointer;gap:12px;font-size:18px;font-weight:700;color:#38bdf8;margin:0;'>"
                      "<input type='checkbox' id='ts_enabled' name='ts_enabled' value='1' onchange='onFieldInput()' style='width:22px;height:22px;accent-color:#0284c7;cursor:pointer;' " + String(ts_enabled ? "checked" : "") + ">"
                      "啟用 Tailscale VPN 遠端穿透"
                      "</label>"
                      "<div style='font-size:14px;color:#94a3b8;margin:8px 0 16px 0;line-height:1.4;'>啟用後可直接填寫 Tailscale 內網 IP (100.x.y.z) 跨網段連線家中 3D 印表機。</div>"
                      "<label style='font-size:16px;'>Tailscale Auth Key (tskey-auth-...):</label>"
                      "<input type='text' id='ts_auth_key' name='ts_auth_key' value='" + ts_auth_key + "' oninput='onFieldInput()' placeholder='tskey-auth-xxxxx' style='margin-bottom:14px;'>"
                      "<label style='font-size:16px;'>Tailnet 裝置名稱 (Device Name):</label>"
                      "<input type='text' id='ts_dev_name' name='ts_dev_name' value='" + ts_dev_name + "' oninput='onFieldInput()' placeholder='snapmatrix-cyd' style='margin-bottom:14px;'>"
                      "<label style='font-size:16px;'>子網路由閘道 IP (Subnet Router，選填):</label>"
                      "<input type='text' id='ts_subnet_router' name='ts_subnet_router' value='" + ts_subnet_router + "' oninput='onFieldInput()' placeholder='例如: 100.64.121.55 (若印表機在遠端 LAN 填寫)' style='margin-bottom:0;'>"
                      "</div>"
                      "<button type='submit'>💾 儲存設定並重新連線</button>"
                      "</form>"
                      "<a class='btn-ota' href='/update'>🚀 開啟 OTA 韌體無線更新</a>"
                      "</div>"
                      "<script>"
                      "var profilesData = {"
                      "'snapmaker_u1':{"
                      "'ip':'" + u1_cfg.ip + "',"
                      "'ts_enabled':" + String(u1_cfg.ts_enabled ? "true" : "false") + ","
                      "'ts_auth_key':'" + u1_cfg.ts_auth_key + "',"
                      "'ts_dev_name':'" + u1_cfg.ts_dev_name + "',"
                      "'ts_subnet':'" + u1_cfg.ts_subnet + "'"
                      "},"
                      "'ideaformer_ir3':{"
                      "'ip':'" + ir3_cfg.ip + "',"
                      "'ts_enabled':" + String(ir3_cfg.ts_enabled ? "true" : "false") + ","
                      "'ts_auth_key':'" + ir3_cfg.ts_auth_key + "',"
                      "'ts_dev_name':'" + ir3_cfg.ts_dev_name + "',"
                      "'ts_subnet':'" + ir3_cfg.ts_subnet + "'"
                      "}"
                      "};"
                      "var curModel = document.getElementById('printer_model').value;"
                      "function onFieldInput(){"
                      "if(!profilesData[curModel])return;"
                      "profilesData[curModel].ip=document.getElementById('printer_ip').value;"
                      "profilesData[curModel].ts_enabled=document.getElementById('ts_enabled').checked;"
                      "profilesData[curModel].ts_auth_key=document.getElementById('ts_auth_key').value;"
                      "profilesData[curModel].ts_dev_name=document.getElementById('ts_dev_name').value;"
                      "profilesData[curModel].ts_subnet=document.getElementById('ts_subnet_router').value;"
                      "}"
                      "function onProfileChange(model){"
                      "curModel=model;"
                      "var d=profilesData[model];"
                      "if(!d)return;"
                      "document.getElementById('printer_ip').value=d.ip;"
                      "document.getElementById('ts_enabled').checked=d.ts_enabled;"
                      "document.getElementById('ts_auth_key').value=d.ts_auth_key;"
                      "document.getElementById('ts_dev_name').value=d.ts_dev_name;"
                      "document.getElementById('ts_subnet_router').value=d.ts_subnet;"
                      "}"
                      "function checkRom(){"
                      "fetch('/api/status').then(r=>r.json()).then(d=>{"
                      "if(d.rom){var el=document.getElementById('rom-text');if(el)el.innerText=d.rom;}"
                      "if(d.connected!==undefined){var cs=document.getElementById('conn-status');if(cs)cs.innerText='狀態: '+(d.connected?'已連線':'未連線');}"
                      "if(d.ts_status){var ts=document.getElementById('ts-status-text');if(ts)ts.innerText=d.ts_status;}"
                      "if(d.rom&&d.rom.indexOf('讀取中')===-1&&d.connected){clearInterval(timer);}"
                      "}).catch(e=>{});}"
                      "var timer=setInterval(checkRom,2000);"
                      "</script></body></html>";
        server.send(200, "text/html", html);
    });

    server.on("/save", HTTP_POST, []() {
        String new_model = server.hasArg("printer_model") ? server.arg("printer_model") : printer_model;
        new_model.trim();
        if (new_model.length() == 0) new_model = "snapmaker_u1";

        bool model_changed = (new_model != printer_model);

        String new_ip = server.hasArg("printer_ip") ? server.arg("printer_ip") : printer_ip;
        new_ip.trim();

        bool new_ts_enabled = server.hasArg("ts_enabled");
        String new_ts_key = server.hasArg("ts_auth_key") ? server.arg("ts_auth_key") : "";
        String new_ts_dev = server.hasArg("ts_dev_name") ? server.arg("ts_dev_name") : DEFAULT_TS_DEV_NAME;
        String new_ts_subnet = server.hasArg("ts_subnet_router") ? server.arg("ts_subnet_router") : "";
        new_ts_key.trim();
        new_ts_dev.trim();
        new_ts_subnet.trim();

        // 儲存至該特定 Profile 的專屬設定
        PrinterProfileConfig cfgToSave;
        cfgToSave.ip = new_ip;
        cfgToSave.ts_enabled = new_ts_enabled;
        cfgToSave.ts_auth_key = new_ts_key;
        cfgToSave.ts_dev_name = new_ts_dev;
        cfgToSave.ts_subnet = new_ts_subnet;
        saveProfileConfig(new_model, cfgToSave);

        // 套用當前選擇的 Profile
        applyProfile(new_model);

        if (model_changed) {
            moonraker.setPrinterProfile(printer_model);
            dashboard_initialized = false;
            Serial.printf("[Preferences] 已儲存並切換印表機機型: %s\n", printer_model.c_str());
        }

        if (server.hasArg("sleep")) {
            sleep_timeout = server.arg("sleep").toInt();
            preferences.putInt("sleep_timeout", sleep_timeout);
            idle_start = millis();
            Serial.printf("[Preferences] 已儲存並套用休眠時間: %d 分鐘\n", sleep_timeout);
        }

        Serial.printf("[Preferences] 已儲存 [%s] 設定: IP=%s, TS啟用=%d, 裝置=%s, SubnetRouter=%s\n", 
                      printer_model.c_str(), printer_ip.c_str(), ts_enabled, ts_dev_name.c_str(), ts_subnet_router.c_str());

        if (ts_enabled && ts_auth_key.length() > 0) {
            tailscaleService.begin(true, ts_auth_key, ts_dev_name, printer_ip, ts_subnet_router);
        } else {
            tailscaleService.stop();
        }

        tailscaleService.setTargetPeer(printer_ip, ts_subnet_router);
        moonraker.setPrinterIP(printer_ip);

        server.sendHeader("Location", "/");
        server.send(303);
    });

    server.on("/reboot", HTTP_GET, []() {
        server.send(200, "text/plain", "Rebooting...");
        delay(500);
        ESP.restart();
    });

    ElegantOTA.begin(&server);
    ElegantOTA.setAutoReboot(true);
    ElegantOTA.onStart([]() {
        Serial.println("[OTA] 韌體更新串流開始！");
        is_updating = true;
        moonraker.ws.disconnect();
    });
    ElegantOTA.onProgress([](size_t current, size_t final) {
        static unsigned long last_log = 0;
        if (millis() - last_log > 1000) {
            last_log = millis();
            Serial.printf("[OTA] 更新進度: %u / %u bytes\n", (unsigned int)current, (unsigned int)final);
        }
    });
    ElegantOTA.onEnd([](bool success) {
        Serial.printf("[OTA] Firmware update %s!\n", success ? "SUCCESSFUL" : "FAILED");
        is_updating = false;
    });
    server.begin();
    Serial.println("[OTA] Web Server started on port 80 /update");

    // Connect to Moonraker
    moonraker.setPrinterProfile(printer_model);
    moonraker.begin(printer_ip, 7125);
}

void loop() {
    server.handleClient();
    ElegantOTA.loop();
    tailscaleService.update();

    if (is_updating) {
        delay(1);
        return; // Prioritize 100% CPU & networking bandwidth for OTA writing
    }

    moonraker.loop();

    // Tailscale 遠端連線監控與握手維持
    if (ts_enabled && printer_ip.startsWith("100.")) {
        static unsigned long last_ts_retry = 0;
        if (!moonraker.state.is_connected && tailscaleService.isConnected()) {
            if (millis() - last_ts_retry >= 15000UL) {
                last_ts_retry = millis();
                Serial.printf("[Tailscale Watchdog] 印表機尚未連線，向 %s 喚醒 WireGuard 握手...\n", printer_ip.c_str());
                tailscaleService.ensurePeerHandshake(printer_ip);
            }
        }
    }

    // Periodic Weather Refresh (Every 15 minutes)
    static unsigned long last_weather_refresh = 0;
    if (millis() - last_weather_refresh >= 15 * 60 * 1000UL) {
        last_weather_refresh = millis();
        weatherService.triggerUpdateAsync();
    }

    static unsigned long last_frame = 0;
    if (millis() - last_frame >= 40) { // ~25 FPS
        last_frame = millis();
        PrinterState pState;
        moonraker.getState(pState);

        // Touch & Hardware Button Input Handling (Toggle between Working Dashboard & Standby Screensaver)
        // Supports both Touchscreens and Non-Touch CYD boards via GPIO 0 (BOOT button)
        static unsigned long last_input_time = 0;
        static bool was_active = false;
        uint16_t touch_x = 0, touch_y = 0;
        bool is_touched = lcd.getTouch(&touch_x, &touch_y);
        bool is_btn_pressed = (digitalRead(0) == LOW);
        bool is_active = (is_touched || is_btn_pressed);

        if (is_active && !was_active && (millis() - last_input_time >= 350)) {
            last_input_time = millis();
            was_active = true;

            const char* trigger_source = is_btn_pressed ? "GPIO 0 (BOOT 按鍵)" : "觸控螢幕";

            if (in_screensaver || manual_sleep) {
                // Wake Up -> Working Mode
                manual_sleep = false;
                in_screensaver = false;
                screensaver_enter_time = 0;
                idle_start = millis();
                dashboard_initialized = false; // Trigger full dashboard redraw
                standby_initialized = false;
                if (current_lcd_brightness != BRIGHTNESS_NORMAL) {
                    current_lcd_brightness = BRIGHTNESS_NORMAL;
                    lcd.setBrightness(current_lcd_brightness);
                }
                Serial.printf("[Input] %s 喚醒 -> 恢復工作狀態與 100%% 亮度\n", trigger_source);
            } else {
                // Sleep -> Standby / Screensaver Mode
                manual_sleep = true;
                in_screensaver = true;
                screensaver_enter_time = millis();
                idle_start = 0;
                dashboard_initialized = false;
                standby_initialized = false;
                Serial.printf("[Input] %s 休眠 -> 切換至待機狀態\n", trigger_source);
            }
        } else if (!is_active) {
            was_active = false;
        }

        // Update virtual LED matrix content
        drawMatrixContent(pState);

        // Render Full Dashboard on 7-inch LCD
        renderDashboardUI(pState);
    }
}
