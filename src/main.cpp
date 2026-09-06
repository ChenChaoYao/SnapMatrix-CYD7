#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <time.h>
#include "DisplayDriver.h"
#include "VirtualMatrix.h"
#include "MoonrakerClient.h"
#include "WeatherService.h"

// Hardware instances
LGFX lcd;
VirtualMatrix matrix;
MoonrakerClient moonraker;
WeatherService weatherService;
WebServer server(80);
Preferences preferences;

// Configuration
String printer_ip = "192.168.1.100";
String display_text = "CONNECTING TO WI-FI...";
String current_display_state = "IDLE";
int x_pos = MATRIX_WIDTH;

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
    uint16_t blue = matrix.Color(0, 128, 255);
    uint16_t green = matrix.Color(0, 255, 0);
    uint16_t dark_gray = matrix.Color(50, 50, 50);
    
    matrix.drawLine(center_x - 4, 7, center_x + 4, 7, dark_gray);
    int probe_x = center_x - 3 + ((millis() / 400) % 7);
    matrix.drawLine(probe_x, 1, probe_x, 5, blue);
    matrix.drawPixel(probe_x, 6, green);
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

    // Moving nozzle head X: oscillates gently between center_x-2 and center_x+2
    int phase = (millis() / 250) % 6;
    int nx = (phase <= 3) ? (center_x - 2 + phase) : (center_x + 4 - phase);

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
        display_text = "PRINTING: " + String(pState.mc_percent) + "% - " + time_str + " - LAYER " + String(pState.layer_num) + "/" + String(pState.total_layer_num) + " - BED: " + String(pState.bed_temper) + "C - NOZZLE: " + String(pState.nozzle_temper) + "C";
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
        
        char hour_str[4];
        char min_str[4];
        strftime(hour_str, sizeof(hour_str), "%H", timeinfo);
        strftime(min_str, sizeof(min_str), "%M", timeinfo);
        
        matrix.fillScreen(0);
        uint16_t active_bg_color = bg_enabled ? scaleColor(current_bg_color, bg_brightness) : 0;
        matrix.fillRect(0, 0, MATRIX_WIDTH, 7, active_bg_color);
        
        // Static centered 24-hour time on the 32x8 matrix (NO SCROLLING!)
        uint16_t t_col = scaleColor(0xFFFFFF, text_brightness);
        matrix.setTextColor(t_col);
        
        // Hour (2 chars: 2*6 = 12px) at X = 2
        matrix.setCursor(2, 0);
        matrix.print(hour_str);
        
        // Blinking Colon (Slower, gentle cadence: 1 sec ON / 1 sec OFF)
        bool colon_on = ((millis() / 1000) % 2 == 0);
        if (colon_on) {
            matrix.setCursor(14, 0);
            matrix.print(":");
        }
        
        // Minute (2 chars: 2*6 = 12px) at X = 19
        matrix.setCursor(19, 0);
        matrix.print(min_str);
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
    lcd.print("SnapMatrix 智慧面板 v1.0");

    // BOTTOM METRICS CARDS (Y=280 to 460)
    int cardY = 280;
    int cardH = 180;

    // Card 1: Status & Progress (X=16, W=240)
    lcd.fillRoundRect(16, cardY, 240, cardH, 12, card_bg);
    lcd.drawRoundRect(16, cardY, 240, cardH, 12, card_border);
    lcd.setFont(&fonts::efontTW_16);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, card_bg);
    lcd.setCursor(30, cardY + 15);
    lcd.print("列印進度");

    // Card 2: Temperatures (X=272, W=240)
    lcd.fillRoundRect(272, cardY, 240, cardH, 12, card_bg);
    lcd.drawRoundRect(272, cardY, 240, cardH, 12, card_border);
    lcd.setFont(&fonts::efontTW_16);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, card_bg);
    lcd.setCursor(286, cardY + 15);
    lcd.print("溫度監控");

    // Card 3: Print Job & Extruders (X=528, W=256)
    lcd.fillRoundRect(528, cardY, 256, cardH, 12, card_bg);
    lcd.drawRoundRect(528, cardY, 256, cardH, 12, card_border);
    lcd.setFont(&fonts::efontTW_16);
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_WHITE, card_bg);
    lcd.setCursor(542, cardY + 15);
    lcd.print("任務與層數");

    // Initial Matrix Render
    matrix.renderToLGFX(&lcd, 16, 65, 24, 9, true);

    lcd.endWrite();
    dashboard_initialized = true;
}

void drawWeatherIconLarge(int cx, int cy, int code) {
    if (code >= 95) {
        // Thunderstorm (雷雨)
        lcd.fillCircle(cx - 14, cy - 8, 16, lcd.color565(120, 140, 170));
        lcd.fillCircle(cx + 14, cy - 4, 20, lcd.color565(150, 170, 200));
        lcd.fillCircle(cx - 3, cy + 4, 18, lcd.color565(100, 120, 150));
        lcd.fillRect(cx - 26, cy + 4, 52, 16, lcd.color565(100, 120, 150));
        // Lightning bolt ⚡
        uint16_t yellow = lcd.color565(255, 225, 0);
        lcd.fillTriangle(cx - 4, cy + 8, cx + 10, cy + 8, cx - 2, cy + 22, yellow);
        lcd.fillTriangle(cx + 2, cy + 20, cx + 10, cy + 20, cx - 6, cy + 40, yellow);
    } else if (code >= 51 && code <= 82) {
        // Rain (下雨 / 毛毛雨)
        lcd.fillCircle(cx - 14, cy - 8, 16, lcd.color565(130, 150, 180));
        lcd.fillCircle(cx + 14, cy - 4, 20, lcd.color565(160, 180, 210));
        lcd.fillCircle(cx - 3, cy + 4, 18, lcd.color565(110, 130, 160));
        lcd.fillRect(cx - 26, cy + 4, 52, 16, lcd.color565(110, 130, 160));
        // Raindrops
        uint16_t blue = lcd.color565(50, 180, 255);
        lcd.fillRoundRect(cx - 16, cy + 24, 4, 10, 2, blue);
        lcd.fillRoundRect(cx - 2, cy + 28, 4, 10, 2, blue);
        lcd.fillRoundRect(cx + 12, cy + 22, 4, 10, 2, blue);
    } else if (code <= 1) {
        // Sun (晴天)
        uint16_t gold = lcd.color565(255, 195, 0);
        uint16_t orange = lcd.color565(255, 120, 0);
        lcd.fillCircle(cx, cy + 6, 20, gold);
        // Sun rays
        for (int i = 0; i < 8; i++) {
            float angle = i * (3.14159f / 4.0f);
            int x1 = cx + cos(angle) * 24;
            int y1 = cy + 6 + sin(angle) * 24;
            int x2 = cx + cos(angle) * 34;
            int y2 = cy + 6 + sin(angle) * 34;
            lcd.drawLine(x1, y1, x2, y2, orange);
            lcd.drawLine(x1 + 1, y1, x2 + 1, y2, orange);
        }
    } else {
        // Cloud (多雲 / 陰天)
        lcd.fillCircle(cx - 15, cy - 6, 18, lcd.color565(160, 190, 230));
        lcd.fillCircle(cx + 15, cy - 2, 22, lcd.color565(200, 225, 255));
        lcd.fillCircle(cx - 4, cy + 6, 20, lcd.color565(140, 170, 210));
        lcd.fillRect(cx - 28, cy + 8, 58, 18, lcd.color565(140, 170, 210));
    }
}

void renderStandbyUI() {
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

        // Column Titles
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(lcd.color565(140, 165, 195), card_bg);
        lcd.setCursor(102, 290); // Shifted right by ~0.5cm
        lcd.print("今日天氣狀況");
        
        lcd.setCursor(350, 290);
        lcd.print("即時環境氣溫");

        lcd.setCursor(600, 290);
        lcd.print("即時相對濕度");

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

            lcd.setFont(&fonts::efontTW_16);
            lcd.setTextSize(2);
            lcd.setTextPadding(0);
            lcd.setTextColor(lcd.color565(170, 200, 235), lcd.color565(20, 28, 44));
            
            lcd.setTextDatum(textdatum_t::top_center);
            lcd.drawString(date_buf, 400, 412);
            lcd.drawString(wday_str, 650, 412);
            lcd.setTextDatum(textdatum_t::top_left);
            lcd.setTextSize(1);
            
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
            
            drawWeatherIconLarge(150, 340, last_weather_code);

            lcd.setFont(&fonts::efontTW_16);
            lcd.setTextSize(2);
            lcd.setTextPadding(0);
            lcd.setTextColor(TFT_CYAN, lcd.color565(20, 28, 44));
            lcd.setTextDatum(textdatum_t::top_center);
            String weather_str = last_weather_city + " " + weatherService.data.desc_cn;
            lcd.drawString(weather_str, 150, 412);
            lcd.setTextDatum(textdatum_t::top_left);
            lcd.setTextSize(1);
            lcd.endWrite();
        }

        if ((int)round(weatherService.data.temp) != last_weather_temp) {
            last_weather_temp = (int)round(weatherService.data.temp);
            lcd.startWrite();
            // Center column: temperature (Shifted right by ~1cm)
            lcd.fillRect(285, 320, 230, 80, lcd.color565(20, 28, 44));
            lcd.setFont(&fonts::Font0);
            lcd.setTextSize(6);
            lcd.setTextPadding(0);
            lcd.setTextColor(lcd.color565(255, 150, 50), lcd.color565(20, 28, 44));
            lcd.setCursor(368, 335);
            lcd.printf("%d", last_weather_temp);
            
            lcd.setFont(&fonts::efontTW_16);
            lcd.setTextSize(1);
            lcd.setCursor(460, 340);
            lcd.print("°C");
            lcd.endWrite();
        }

        if (weatherService.data.humidity != last_weather_humidity) {
            last_weather_humidity = weatherService.data.humidity;
            lcd.startWrite();
            // Right column: humidity (Shifted right by ~1cm)
            lcd.fillRect(535, 320, 230, 80, lcd.color565(20, 28, 44));
            lcd.setFont(&fonts::Font0);
            lcd.setTextSize(6);
            lcd.setTextPadding(0);
            lcd.setTextColor(lcd.color565(60, 190, 255), lcd.color565(20, 28, 44));
            lcd.setCursor(618, 335);
            lcd.printf("%d", last_weather_humidity);
            
            lcd.setFont(&fonts::Font0);
            lcd.setTextSize(3);
            lcd.setCursor(708, 340);
            lcd.print("%");
            lcd.endWrite();
        }
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
    
    // Card 3 cache
    static int last_layer_num = -1;
    static int last_total_layer_num = -1;
    static String last_gcode_file = "INIT_UNSET";
    static String last_t_state[4] = {"", "", "", ""};
    static uint32_t last_filament_rgb[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    static bool last_filament_loaded[4] = {false, false, false, false};

    if (in_screensaver) {
        // 1. Render Virtual Matrix (Static 24h Clock)
        lcd.startWrite();
        matrix.renderToLGFX(&lcd, 16, 65, 24, 9, false);
        lcd.endWrite();

        // 2. Render Full High-Def Weather & Humidity Dashboard underneath
        renderStandbyUI();
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
    if (first_run || pState.mc_percent != last_mc_percent) {
        last_mc_percent = pState.mc_percent;
        lcd.fillRect(25, cardY + 40, 220, 38, card_bg);
        lcd.setFont(&fonts::Font0);
        lcd.setTextSize(4);
        lcd.setTextPadding(0);
        lcd.setTextColor(TFT_CYAN, card_bg);
        lcd.setCursor(30, cardY + 45);
        lcd.printf("%d%%", last_mc_percent);
        lcd.setTextSize(1);
    }

    if (first_run || pState.gcode_state != last_gcode_state) {
        last_gcode_state = pState.gcode_state;
        lcd.fillRect(25, cardY + 80, 220, 26, card_bg);
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(TFT_LIGHTGRAY, card_bg);
        lcd.setCursor(30, cardY + 85);
        lcd.printf("狀態: %s", get_status_chinese(last_gcode_state).c_str());
    }

    static bool last_weather_valid = false;
    static int last_weather_code = -1;
    static int last_weather_temp = -999;
    if (first_run || pState.mc_remaining_time != last_remaining_time || pState.gcode_state != last_gcode_state || 
        weatherService.data.valid != last_weather_valid || weatherService.data.code != last_weather_code || (int)weatherService.data.temp != last_weather_temp) {
        last_remaining_time = pState.mc_remaining_time;
        last_weather_valid = weatherService.data.valid;
        last_weather_code = weatherService.data.code;
        last_weather_temp = (int)weatherService.data.temp;
        lcd.fillRect(25, cardY + 108, 220, 26, card_bg);
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(TFT_LIGHTGRAY, card_bg);
        lcd.setCursor(30, cardY + 112);
        if (pState.gcode_state == "IDLE" && weatherService.data.valid) {
            lcd.printf("天氣: %s %d°C %s", weatherService.data.city.c_str(), (int)round(weatherService.data.temp), weatherService.data.desc_cn.c_str());
        } else {
            lcd.printf("剩餘: %s", last_remaining_time >= 0 ? (format_time(last_remaining_time)).c_str() : "--");
        }
    }

    String cur_ota_url = "OTA: http://" + cur_device_ip + "/update";
    if (first_run || cur_ota_url != last_ota_url) {
        last_ota_url = cur_ota_url;
        lcd.fillRect(25, cardY + 140, 220, 22, card_bg);
        lcd.setFont(&fonts::Font0);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(lcd.color565(130, 150, 180), card_bg);
        lcd.setCursor(30, cardY + 145);
        lcd.print(last_ota_url);
    }

    // CARD 2: Temperatures
    if (first_run || pState.nozzle_temper != last_nozzle_temper || pState.nozzle_target_temper != last_nozzle_target) {
        last_nozzle_temper = pState.nozzle_temper;
        last_nozzle_target = pState.nozzle_target_temper;
        lcd.fillRect(280, cardY + 45, 220, 28, card_bg);
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(lcd.color565(255, 100, 100), card_bg);
        lcd.setCursor(286, cardY + 50);
        lcd.printf("噴頭: %d / %d °C", last_nozzle_temper, last_nozzle_target);
    }

    if (first_run || pState.bed_temper != last_bed_temper || pState.bed_target_temper != last_bed_target) {
        last_bed_temper = pState.bed_temper;
        last_bed_target = pState.bed_target_temper;
        lcd.fillRect(280, cardY + 80, 220, 28, card_bg);
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(lcd.color565(100, 180, 255), card_bg);
        lcd.setCursor(286, cardY + 85);
        lcd.printf("熱床: %d / %d °C", last_bed_temper, last_bed_target);
    }

    if (first_run || pState.chamber_temper != last_chamber_temper) {
        last_chamber_temper = pState.chamber_temper;
        lcd.fillRect(280, cardY + 115, 220, 28, card_bg);
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(lcd.color565(150, 255, 150), card_bg);
        lcd.setCursor(286, cardY + 120);
        lcd.printf("機箱: %d °C", last_chamber_temper);
    }

    // CARD 3: Layers & Extruders
    if (first_run || pState.layer_num != last_layer_num || pState.total_layer_num != last_total_layer_num) {
        last_layer_num = pState.layer_num;
        last_total_layer_num = pState.total_layer_num;
        lcd.fillRect(535, cardY + 45, 240, 28, card_bg);
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(TFT_YELLOW, card_bg);
        lcd.setCursor(542, cardY + 50);
        lcd.printf("層數: %d / %d", last_layer_num, last_total_layer_num);
    }

    if (first_run || pState.gcode_file != last_gcode_file) {
        last_gcode_file = pState.gcode_file;
        lcd.fillRect(535, cardY + 80, 240, 28, card_bg);
        lcd.setFont(&fonts::efontTW_16);
        lcd.setTextSize(1);
        lcd.setTextPadding(0);
        lcd.setTextColor(TFT_LIGHTGRAY, card_bg);
        lcd.setCursor(542, cardY + 85);
        String fname = last_gcode_file;
        if (fname.length() > 18) fname = fname.substring(0, 15) + "...";
        lcd.printf("檔案: %s", fname.length() > 0 ? fname.c_str() : "無任務");
    }

    // 4 Slots Indicators (T1 ~ T4 Real Filament Colors - Enlarged Display)
    for (int i = 0; i < 4; i++) {
        if (first_run || pState.t_state[i] != last_t_state[i] || pState.filament_rgb[i] != last_filament_rgb[i] || pState.filament_loaded[i] != last_filament_loaded[i]) {
            last_t_state[i] = pState.t_state[i];
            last_filament_rgb[i] = pState.filament_rgb[i];
            last_filament_loaded[i] = pState.filament_loaded[i];

            int cx = 562 + i * 60;
            int cy = cardY + 138;
            bool loaded = pState.filament_loaded[i] && (pState.t_state[i].length() == 0 || pState.t_state[i] != "wait_insert");
            
            uint8_t r = (pState.filament_rgb[i] >> 16) & 0xFF;
            uint8_t g = (pState.filament_rgb[i] >> 8) & 0xFF;
            uint8_t b = pState.filament_rgb[i] & 0xFF;
            // Hardware RGB Pin mapping: pass (b, g, r) to match Sunton 7" LCD bus wiring
            uint16_t fill_color = loaded ? lcd.color565(b, g, r) : lcd.color565(55, 42, 35);
            
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

    first_run = false;
    lcd.endWrite();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[系統] SnapMatrix CYD 7吋面板啟動中...");

    // Stable Wi-Fi Tx Power
    WiFi.setTxPower(WIFI_POWER_15dBm);

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
    lcd.drawString("SnapMatrix CYD-7 智慧面板 v1.0", 400, 180);

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
    printer_ip = preferences.getString("printer_ip", "192.168.1.100");
    sleep_timeout = preferences.getInt("sleep_timeout", 10);
    Serial.printf("[Preferences] 開機載入休眠時間: %d 分鐘\n", sleep_timeout);

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
        lcd.drawString("SnapMatrix 初始配網模式 v1.0", 400, 80);
        
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
                      ",\"rom\":\"" + rom_info + "\"}";
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

        String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                      "<title>SnapMatrix CYD-7 控制面板 v1.0</title><style>"
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
                      "<h2>SnapMatrix 控制面板 <span style='font-size:15px;background:rgba(56,189,248,0.18);border:1px solid rgba(56,189,248,0.3);color:#38bdf8;padding:3px 10px;border-radius:20px;font-weight:700;margin-left:auto;'>v1.0 正式版</span></h2>"
                      "<div class='status'>"
                      "<div class='status-row'><span>裝置 IP: " + WiFi.localIP().toString() + "</span><span id='conn-status'>狀態: " + (pState.is_connected ? "已連線" : "未連線") + "</span></div>"
                      "<div class='status-row'><span style='color:#94a3b8;'>韌體版本:</span><span style='font-weight:600;color:#38bdf8;'>v1.0</span></div>"
                      "<div class='status-row'><span style='color:#a78bfa;'>🖨️ 印表機 ROM:</span><span id='rom-text' style='font-weight:600;color:#fff;'>" + rom_info + "</span></div>"
                      "<div class='status-row'><span style='color:#38bdf8;'>⛅ 當前天氣:</span><span>" + weather_status + "</span></div>"
                      "</div>"
                      "<form action='/save' method='POST'>"
                      "<label>Moonraker 印表機 IP (Snapmaker):</label>"
                      "<input type='text' name='printer_ip' value='" + printer_ip + "' placeholder='例如: 192.168.3.122' required>"
                      "<label>螢幕自動休眠時間 (分鐘，0 為不休眠):</label>"
                      "<input type='number' name='sleep' value='" + String(sleep_timeout) + "' min='0' max='120'>"
                      "<button type='submit'>💾 儲存設定並重新連線</button>"
                      "</form>"
                      "<a class='btn-ota' href='/update'>🚀 開啟 OTA 韌體無線更新</a>"
                      "</div>"
                      "<script>"
                      "function checkRom(){"
                      "fetch('/api/status').then(r=>r.json()).then(d=>{"
                      "if(d.rom){var el=document.getElementById('rom-text');if(el)el.innerText=d.rom;}"
                      "if(d.connected!==undefined){var cs=document.getElementById('conn-status');if(cs)cs.innerText='狀態: '+(d.connected?'已連線':'未連線');}"
                      "if(d.rom&&d.rom.indexOf('讀取中')===-1&&d.connected){clearInterval(timer);}"
                      "}).catch(e=>{});}"
                      "var timer=setInterval(checkRom,2000);"
                      "</script></body></html>";
        server.send(200, "text/html", html);
    });

    server.on("/save", HTTP_POST, []() {
        if (server.hasArg("printer_ip")) {
            String new_ip = server.arg("printer_ip");
            new_ip.trim();
            if (new_ip.length() > 0) {
                printer_ip = new_ip;
                preferences.putString("printer_ip", printer_ip);
                moonraker.setPrinterIP(printer_ip);
            }
        }
        if (server.hasArg("sleep")) {
            sleep_timeout = server.arg("sleep").toInt();
            preferences.putInt("sleep_timeout", sleep_timeout);
            idle_start = millis();
            Serial.printf("[Preferences] 已儲存並套用休眠時間: %d 分鐘\n", sleep_timeout);
        }
        server.sendHeader("Location", "/");
        server.send(303);
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
    moonraker.begin(printer_ip, 7125);
}

void loop() {
    server.handleClient();
    ElegantOTA.loop();

    if (is_updating) {
        delay(1);
        return; // Prioritize 100% CPU & networking bandwidth for OTA writing
    }

    moonraker.loop();

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

        // Touch Input Handling (Toggle between Working Dashboard & Standby Screensaver)
        static unsigned long last_touch_time = 0;
        static bool was_touched = false;
        uint16_t touch_x, touch_y;
        bool is_touched = lcd.getTouch(&touch_x, &touch_y);

        if (is_touched && !was_touched && (millis() - last_touch_time >= 400)) {
            last_touch_time = millis();
            was_touched = true;

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
                Serial.printf("[Touch] 觸控喚醒 -> 恢復工作狀態與 100%% 亮度 (X:%d, Y:%d)\n", touch_x, touch_y);
            } else {
                // Sleep -> Standby / Screensaver Mode
                manual_sleep = true;
                in_screensaver = true;
                screensaver_enter_time = millis();
                idle_start = 0;
                dashboard_initialized = false;
                standby_initialized = false;
                Serial.printf("[Touch] 觸控休眠 -> 切換至待機狀態 (X:%d, Y:%d)\n", touch_x, touch_y);
            }
        } else if (!is_touched) {
            was_touched = false;
        }

        // Update virtual LED matrix content
        drawMatrixContent(pState);

        // Render Full Dashboard on 7-inch LCD
        renderDashboardUI(pState);
    }
}
