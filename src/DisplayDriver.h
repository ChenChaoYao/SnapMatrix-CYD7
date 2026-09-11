#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// Configuration for Sunton / CYD ESP32-8048S070 (7.0" 800x480 RGB Display with GT911 Touch)
class LGFX : public lgfx::LGFX_Device
{
public:
    lgfx::Bus_RGB     _bus_instance;
    lgfx::Panel_RGB   _panel_instance;
    lgfx::Light_PWM   _light_instance;
    lgfx::Touch_GT911 _touch_instance;

    LGFX(void)
    {
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;
            
#if defined(BOARD_CYD_50)
            // =========================================================================
            // Configuration for Sunton / CYD ESP32-8048S050 (5.0" 800x480 RGB Display)
            // =========================================================================
            // Blue Pins (B0 ~ B4)
            cfg.pin_d0  = GPIO_NUM_8;  // B0
            cfg.pin_d1  = GPIO_NUM_3;  // B1
            cfg.pin_d2  = GPIO_NUM_46; // B2
            cfg.pin_d3  = GPIO_NUM_9;  // B3
            cfg.pin_d4  = GPIO_NUM_1;  // B4

            // Green Pins (G0 ~ G5)
            cfg.pin_d5  = GPIO_NUM_5;  // G0
            cfg.pin_d6  = GPIO_NUM_6;  // G1
            cfg.pin_d7  = GPIO_NUM_7;  // G2
            cfg.pin_d8  = GPIO_NUM_15; // G3
            cfg.pin_d9  = GPIO_NUM_16; // G4
            cfg.pin_d10 = GPIO_NUM_4;  // G5

            // Red Pins (R0 ~ R4)
            cfg.pin_d11 = GPIO_NUM_45; // R0
            cfg.pin_d12 = GPIO_NUM_48; // R1
            cfg.pin_d13 = GPIO_NUM_47; // R2
            cfg.pin_d14 = GPIO_NUM_21; // R3
            cfg.pin_d15 = GPIO_NUM_14; // R4

            // Control Signals (Note: DE and VSYNC are swapped compared to 7.0" board)
            cfg.pin_henable = GPIO_NUM_40; // DE
            cfg.pin_vsync   = GPIO_NUM_41; // VSYNC
            cfg.pin_hsync   = GPIO_NUM_39; // HSYNC
            cfg.pin_pclk    = GPIO_NUM_42; // PCLK
            cfg.freq_write  = 13000000;    // 13MHz pixel clock for ST7262 5.0" panel

            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch  = 16;
            
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 4;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 4;

            cfg.pclk_active_neg   = 1;
            cfg.de_idle_high      = 0;
            cfg.pclk_idle_high    = 1;
#else
            // =========================================================================
            // Configuration for Sunton / CYD ESP32-8048S070 (7.0" 800x480 RGB Display)
            // =========================================================================
            // Red Pins (R0 ~ R4)
            cfg.pin_d0  = GPIO_NUM_14; // R0
            cfg.pin_d1  = GPIO_NUM_21; // R1
            cfg.pin_d2  = GPIO_NUM_47; // R2
            cfg.pin_d3  = GPIO_NUM_48; // R3
            cfg.pin_d4  = GPIO_NUM_45; // R4
            
            // Green Pins (G0 ~ G5)
            cfg.pin_d5  = GPIO_NUM_9;  // G0
            cfg.pin_d6  = GPIO_NUM_46; // G1
            cfg.pin_d7  = GPIO_NUM_3;  // G2
            cfg.pin_d8  = GPIO_NUM_8;  // G3
            cfg.pin_d9  = GPIO_NUM_16; // G4
            cfg.pin_d10 = GPIO_NUM_1;  // G5
            
            // Blue Pins (B0 ~ B4)
            cfg.pin_d11 = GPIO_NUM_15; // B0
            cfg.pin_d12 = GPIO_NUM_7;  // B1
            cfg.pin_d13 = GPIO_NUM_6;  // B2
            cfg.pin_d14 = GPIO_NUM_5;  // B3
            cfg.pin_d15 = GPIO_NUM_4;  // B4

            // Control Signals
            cfg.pin_henable = GPIO_NUM_41; // DE
            cfg.pin_vsync   = GPIO_NUM_40; // VSYNC
            cfg.pin_hsync   = GPIO_NUM_39; // HSYNC
            cfg.pin_pclk    = GPIO_NUM_42; // PCLK
            // Optimized Low-Jitter Standard Timing for Sunton 7.0-inch 800x480 RGB Panel
            cfg.freq_write  = 14000000;    // 14MHz pixel clock for rock-solid PSRAM DMA stability

            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 40;
            cfg.hsync_pulse_width = 48;
            cfg.hsync_back_porch  = 40;
            
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 13;
            cfg.vsync_pulse_width = 3;
            cfg.vsync_back_porch  = 32;

            cfg.pclk_active_neg   = 1;
            cfg.de_idle_high      = 0;
            cfg.pclk_idle_high    = 0;
#endif

            _bus_instance.config(cfg);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.memory_width  = 800;
            cfg.memory_height = 480;
            cfg.panel_width   = 800;
            cfg.panel_height  = 480;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            _panel_instance.config(cfg);
        }

        {
            auto cfg = _panel_instance.config_detail();
            cfg.use_psram = 1;
            _panel_instance.config_detail(cfg);
        }

        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = GPIO_NUM_2; // Backlight Pin
            cfg.invert = false;
            cfg.freq   = 20000;      // 20kHz High frequency PWM to eliminate visible beat flickering
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
        }

        {
            auto cfg = _touch_instance.config();
            cfg.x_min      = 0;
            cfg.x_max      = 799;
            cfg.y_min      = 0;
            cfg.y_max      = 479;
            cfg.pin_int    = -1;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port   = 1;
            cfg.pin_sda    = GPIO_NUM_19;
            cfg.pin_scl    = GPIO_NUM_20;
#if defined(BOARD_CYD_50)
            cfg.pin_rst    = -1;          // Most 5.0" boards do not connect GT911 RST to GPIO38
#else
            cfg.pin_rst    = GPIO_NUM_38;
#endif
            cfg.freq       = 400000;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        _panel_instance.setBus(&_bus_instance);
        _panel_instance.setLight(&_light_instance);
        setPanel(&_panel_instance);
    }
};
