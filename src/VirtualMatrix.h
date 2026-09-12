#pragma once
#include <Arduino.h>
#include <LovyanGFX.hpp>

#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT 8

// 5x7 ASCII Font definitions (32 to 127)
static const unsigned char font5x7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // Space (32)
    0x00, 0x00, 0x5F, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $
    0x23, 0x13, 0x08, 0x64, 0x62, // %
    0x36, 0x49, 0x55, 0x22, 0x50, // &
    0x00, 0x05, 0x03, 0x00, 0x00, // '
    0x00, 0x1C, 0x22, 0x41, 0x00, // (
    0x00, 0x41, 0x22, 0x1C, 0x00, // )
    0x08, 0x2A, 0x1C, 0x2A, 0x08, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, // +
    0x00, 0x50, 0x30, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, // -
    0x00, 0x60, 0x60, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, // ;
    0x00, 0x08, 0x14, 0x22, 0x41, // <
    0x14, 0x14, 0x14, 0x14, 0x14, // =
    0x41, 0x22, 0x14, 0x08, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, // E
    0x7F, 0x09, 0x09, 0x01, 0x01, // F
    0x3E, 0x41, 0x41, 0x51, 0x32, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, // L
    0x7F, 0x02, 0x04, 0x02, 0x7F, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, // R
    0x46, 0x49, 0x49, 0x49, 0x31, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V
    0x7F, 0x20, 0x18, 0x20, 0x7F, // W
    0x63, 0x14, 0x08, 0x14, 0x63, // X
    0x03, 0x04, 0x78, 0x04, 0x03, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, // Z
    0x00, 0x7F, 0x41, 0x41, 0x00, // [
    0x02, 0x04, 0x08, 0x10, 0x20, // \
    0x00, 0x41, 0x41, 0x7F, 0x00, // ]
    0x04, 0x02, 0x01, 0x02, 0x04, // ^
    0x40, 0x40, 0x40, 0x40, 0x40, // _
    0x00, 0x01, 0x02, 0x04, 0x00, // `
    0x20, 0x54, 0x54, 0x54, 0x78, // a
    0x7F, 0x48, 0x44, 0x44, 0x38, // b
    0x38, 0x44, 0x44, 0x44, 0x20, // c
    0x38, 0x44, 0x44, 0x48, 0x7F, // d
    0x38, 0x54, 0x54, 0x54, 0x18, // e
    0x08, 0x7E, 0x09, 0x01, 0x02, // f
    0x08, 0x14, 0x54, 0x54, 0x3C, // g
    0x7F, 0x08, 0x04, 0x04, 0x78, // h
    0x00, 0x44, 0x7D, 0x40, 0x00, // i
    0x20, 0x40, 0x44, 0x3D, 0x00, // j
    0x00, 0x7F, 0x10, 0x28, 0x44, // k
    0x00, 0x41, 0x7F, 0x40, 0x00, // l
    0x7C, 0x04, 0x18, 0x04, 0x78, // m
    0x7C, 0x08, 0x04, 0x04, 0x78, // n
    0x38, 0x44, 0x44, 0x44, 0x38, // o
    0x7C, 0x14, 0x14, 0x14, 0x08, // p
    0x08, 0x14, 0x14, 0x18, 0x7C, // q
    0x7C, 0x08, 0x04, 0x04, 0x08, // r
    0x48, 0x54, 0x54, 0x54, 0x20, // s
    0x04, 0x3F, 0x44, 0x40, 0x20, // t
    0x3C, 0x40, 0x40, 0x20, 0x7C, // u
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w
    0x44, 0x28, 0x10, 0x28, 0x44, // x
    0x0C, 0x50, 0x50, 0x50, 0x3C, // y
    0x44, 0x64, 0x54, 0x4C, 0x44, // z
    0x00, 0x08, 0x36, 0x41, 0x00, // {
    0x00, 0x00, 0x7F, 0x00, 0x00, // |
    0x00, 0x41, 0x36, 0x08, 0x00, // }
    0x08, 0x08, 0x2A, 0x1C, 0x08  // ~
};

// Bold Modern Digital Clock Font (5x7) for 0~9
static const uint8_t clockDigits5x7[10][5] = {
    { 0x3E, 0x63, 0x41, 0x63, 0x3E }, // 0
    { 0x00, 0x42, 0x7F, 0x7F, 0x40 }, // 1
    { 0x62, 0x73, 0x59, 0x4F, 0x46 }, // 2
    { 0x22, 0x63, 0x49, 0x7F, 0x36 }, // 3
    { 0x18, 0x1C, 0x16, 0x7F, 0x7F }, // 4
    { 0x27, 0x67, 0x45, 0x7D, 0x39 }, // 5
    { 0x3E, 0x7F, 0x49, 0x7B, 0x32 }, // 6
    { 0x41, 0x71, 0x3D, 0x0F, 0x03 }, // 7
    { 0x36, 0x7F, 0x49, 0x7F, 0x36 }, // 8
    { 0x26, 0x6F, 0x49, 0x7F, 0x3E }  // 9
};


class VirtualMatrix {
public:
    uint16_t buffer[MATRIX_WIDTH][MATRIX_HEIGHT];
    uint16_t prev_buffer[MATRIX_WIDTH][MATRIX_HEIGHT];
    bool force_full_redraw = true;
    uint16_t textColor = 0xFFFF;
    int cursorX = 0;
    int cursorY = 0;
    bool textWrap = false;

    VirtualMatrix() {
        clear();
        memset(prev_buffer, 0xFF, sizeof(prev_buffer));
    }

    void clear() {
        memset(buffer, 0, sizeof(buffer));
    }

    void invalidate() {
        force_full_redraw = true;
    }

    void fillScreen(uint16_t color) {
        for (int x = 0; x < MATRIX_WIDTH; x++) {
            for (int y = 0; y < MATRIX_HEIGHT; y++) {
                buffer[x][y] = color;
            }
        }
    }

    void drawPixel(int x, int y, uint16_t color) {
        if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
            buffer[x][y] = color;
        }
    }

    uint16_t getPixel(int x, int y) {
        if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
            return buffer[x][y];
        }
        return 0;
    }

    void drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;
        while (true) {
            drawPixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void fillRect(int x, int y, int w, int h, uint16_t color) {
        for (int i = x; i < x + w; i++) {
            for (int j = y; j < y + h; j++) {
                drawPixel(i, j, color);
            }
        }
    }

    void setTextColor(uint16_t color) {
        textColor = color;
    }

    void setCursor(int x, int y) {
        cursorX = x;
        cursorY = y;
    }

    void setTextWrap(bool wrap) {
        textWrap = wrap;
    }

    uint16_t Color(uint8_t r, uint8_t g, uint8_t b) {
        // Convert RGB888 to RGB565
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    int clipMinX = 0;
    int clipMaxX = MATRIX_WIDTH - 1;

    void setClip(int minX, int maxX = MATRIX_WIDTH - 1) {
        clipMinX = minX;
        clipMaxX = maxX;
    }

    void resetClip() {
        clipMinX = 0;
        clipMaxX = MATRIX_WIDTH - 1;
    }

    int width() const { return MATRIX_WIDTH; }
    int height() const { return MATRIX_HEIGHT; }

    // 5x7 Standard Matrix Font Drawer
    void drawChar(int x, int y, unsigned char c, uint16_t color) {
        if (c < 32 || c > 127) c = '?';
        c -= 32;
        for (int i = 0; i < 5; i++) {
            int px = x + i;
            if (px < clipMinX || px > clipMaxX) continue;
            uint8_t line = font5x7[c * 5 + i];
            for (int j = 0; j < 7; j++) {
                if (line & 0x1) {
                    drawPixel(px, y + j, color);
                }
                line >>= 1;
            }
        }
    }

    void print(const String& str) {
        int x = cursorX;
        for (size_t i = 0; i < str.length(); i++) {
            drawChar(x, cursorY, str[i], textColor);
            x += 6; // 5 width + 1 spacing
        }
        cursorX = x;
    }

    // Draw Clock Digit (5x7)
    void drawClockDigit(int x, int y, int digit, uint16_t color) {
        if (digit < 0 || digit > 9) return;
#if defined(BOARD_CYD_50)
        // 5.0 吋面板：經典原版細體字型 (font5x7 原生細體)
        drawChar(x, y, '0' + digit, color);
#else
        // 7.0 吋面板：現代加粗數位時鐘字體 (clockDigits5x7)
        for (int i = 0; i < 5; i++) {
            int px = x + i;
            if (px < 0 || px >= MATRIX_WIDTH) continue;
            uint8_t line = clockDigits5x7[digit][i];
            for (int j = 0; j < 7; j++) {
                if (line & 0x1) {
                    drawPixel(px, y + j, color);
                }
                line >>= 1;
            }
        }
#endif
    }

    // Draw Clock Colon
    void drawClockColon(int x, int y, uint16_t color) {
#if defined(BOARD_CYD_50)
        // 5.0 吋面板：原版經典細體冒號 (調用 font5x7 之 ':')
        drawChar(x - 1, y, ':', color);
#else
        // 7.0 吋面板：現代加粗方塊冒號 (2x2 方塊點)
        for (int i = 0; i < 2; i++) {
            int px = x + i;
            if (px < 0 || px >= MATRIX_WIDTH) continue;
            drawPixel(px, y + 1, color);
            drawPixel(px, y + 2, color);
            drawPixel(px, y + 4, color);
            drawPixel(px, y + 5, color);
        }
#endif
    }

    // Render Centered 24h Clock (26 dots total width, perfectly centered with 3 dots margins)
    void drawClockTime(int hour, int minute, bool colon_on, uint16_t color, int y = 0) {
        // Hour (2 digits)
        drawClockDigit(3, y, hour / 10, color);
        drawClockDigit(9, y, hour % 10, color);

        // Blinking Colon
        if (colon_on) {
            drawClockColon(15, y, color);
        }

        // Minute (2 digits)
        drawClockDigit(18, y, minute / 10, color);
        drawClockDigit(24, y, minute % 10, color);
    }


    // Flicker-Free Differential Render into LovyanGFX Display
    void renderToLGFX(LovyanGFX* canvas, int startX = 16, int startY = 65, int pitch = 24, int dotRadius = 9, bool forceAll = false) {
        for (int x = 0; x < MATRIX_WIDTH; x++) {
            for (int y = 0; y < MATRIX_HEIGHT; y++) {
                uint16_t col = buffer[x][y];
                if (!forceAll && !force_full_redraw && col == prev_buffer[x][y]) {
                    continue;
                }
                prev_buffer[x][y] = col;

                int cx = startX + x * pitch + pitch / 2;
                int cy = startY + y * pitch + pitch / 2;

                if (col != 0) {
                    // Glow halo (outer)
                    uint8_t r = (col >> 11) << 3;
                    uint8_t g = ((col >> 5) & 0x3F) << 2;
                    uint8_t b = (col & 0x1F) << 3;
                    uint16_t glowCol = Color(r / 3, g / 3, b / 3);
                    canvas->fillCircle(cx, cy, dotRadius + 2, glowCol);
                    
                    // Main LED dot
                    canvas->fillCircle(cx, cy, dotRadius, col);
                    
                    // Specular highlight (center shine)
                    uint16_t highlight = Color(min(255, r + 100), min(255, g + 100), min(255, b + 100));
                    canvas->fillCircle(cx - 2, cy - 2, 2, highlight);
                } else {
                    // Dark LED pixel (Off state) - erase glow and redraw base LED
                    canvas->fillCircle(cx, cy, dotRadius + 2, canvas->color565(12, 16, 24));
                    canvas->fillCircle(cx, cy, dotRadius, canvas->color565(18, 22, 30));
                    canvas->drawCircle(cx, cy, dotRadius, canvas->color565(32, 38, 50));
                }
            }
        }
        force_full_redraw = false;
    }
};
