#include "display.h"
#include "st7735.h"
#include "fonts.h"
#include "ld2412.h"
#include <stdio.h>

// สีเทาหม่นสำหรับเส้นคั่นและสถานะที่ไม่ active (แทน White/Red เดิมที่จ้าเกินไป)
#define COLOR_DIVIDER   ST7735_COLOR565(40, 40, 40)
#define COLOR_INACTIVE  ST7735_COLOR565(90, 90, 90)
#define COLOR_DIM_BLUE  ST7735_COLOR565(40, 70, 110)

static void DrawDivider(uint16_t y) {
    ST7735_FillRectangle(0, y, 128, 1, COLOR_DIVIDER);
}

void Display_Update(SensorData_t *data,
                    DS3231_Time_t *rtc,
                    uint8_t light_on,
                    uint8_t wifi_ok,
                    uint8_t i2c_error_count) {
    char buf[32];

    // ════════ Header: เวลา + Wi-Fi ════════
    sprintf(buf, "%02d:%02d:%02d", rtc->hour, rtc->min, rtc->sec);
    ST7735_WriteString(4, 4, buf, Font_11x18, ST7735_WHITE, ST7735_BLACK);

    sprintf(buf, "%s", wifi_ok ? "WIFI" : "OFF ");
    ST7735_WriteString(96, 6, buf, Font_7x10, wifi_ok ? ST7735_GREEN : COLOR_INACTIVE, ST7735_BLACK);

    DrawDivider(24);

    // ════════ Environment: อุณหภูมิ/ความชื้น/แสง ════════
    sprintf(buf, "T:%.1fC  H:%.0f%%   ", data->temperature, data->humidity);
    ST7735_WriteString(4, 30, buf, Font_7x10, ST7735_YELLOW, ST7735_BLACK);

    sprintf(buf, "Lux: %.0f      ", data->lux);
    ST7735_WriteString(4, 43, buf, Font_7x10, (data->lux >= 50) ? ST7735_CYAN : COLOR_DIM_BLUE, ST7735_BLACK);

    DrawDivider(56);

    // ════════ Radar & Light ════════
    ST7735_WriteString(4, 62, "RADAR:", Font_7x10, COLOR_INACTIVE, ST7735_BLACK);
    sprintf(buf, "%s    ", data->motion ? "DETECTED" : "CLEAR   ");
    ST7735_WriteString(52, 62, buf, Font_7x10, data->motion ? ST7735_GREEN : COLOR_INACTIVE, ST7735_BLACK);

    ST7735_WriteString(4, 75, "LIGHT:", Font_7x10, COLOR_INACTIVE, ST7735_BLACK);
    if (light_on) {
        sprintf(buf, "ON %s   ", data->motion ? "(AUTO)" : "(MAN)");
    } else {
        sprintf(buf, "OFF          ");
    }
    ST7735_WriteString(52, 75, buf, Font_7x10, light_on ? ST7735_YELLOW : COLOR_INACTIVE, ST7735_BLACK);

    DrawDivider(88);

    // ════════ Presence (LD2412) ════════
    ST7735_WriteString(4, 94, "PRESENCE", Font_7x10, ST7735_MAGENTA, ST7735_BLACK);

    uint16_t presence_color;
    switch (data->presence_state) {
        case LD2412_STATE_STATIC:        presence_color = ST7735_CYAN;  break;
        case LD2412_STATE_MOTION_STATIC: presence_color = ST7735_GREEN; break;
        default:                         presence_color = COLOR_INACTIVE; break;
    }
    sprintf(buf, "%-14s", LD2412_PresenceToString(data->presence_state));
    ST7735_WriteString(4, 107, buf, Font_7x10, presence_color, ST7735_BLACK);

    // ระยะทาง: แยกคนละบรรทัด ตัวเลขชิดขวา (กว้าง 3) เวลาค่าเปลี่ยนหลักตัวเลขจะได้ไม่กระโดด
    // ถ้าไม่มีเป้าหมายให้โชว์ "--" แทน 0 เพราะค่าระยะตอนนั้นไม่มีความหมาย
    uint8_t has_motion = (data->presence_state == LD2412_STATE_MOTION_STATIC);
    uint8_t has_static = (data->presence_state != LD2412_STATE_NONE);
    char mval[8], sval[8];

    if (has_motion) snprintf(mval, sizeof(mval), "%3u", data->motion_dist_cm);
    else            snprintf(mval, sizeof(mval), " --");

    if (has_static) snprintf(sval, sizeof(sval), "%3u", data->static_dist_cm);
    else            snprintf(sval, sizeof(sval), " --");

    sprintf(buf, "MOTION  %s cm ", mval);
    ST7735_WriteString(4, 120, buf, Font_7x10, has_motion ? ST7735_GREEN : COLOR_INACTIVE, ST7735_BLACK);

    sprintf(buf, "STATIC  %s cm ", sval);
    ST7735_WriteString(4, 132, buf, Font_7x10, has_static ? ST7735_CYAN : COLOR_INACTIVE, ST7735_BLACK);

    DrawDivider(145);

    // ════════ Footer: I2C Health ════════
    ST7735_FillRectangle(4, 150, 6, 6, (i2c_error_count == 0) ? ST7735_GREEN : ST7735_RED);
    ST7735_WriteString(14, 149, "I2C", Font_7x10, COLOR_INACTIVE, ST7735_BLACK);
}
