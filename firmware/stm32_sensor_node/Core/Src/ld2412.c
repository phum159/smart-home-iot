#include "ld2412.h"
#include <string.h>

#define LD2412_RX_BUF_SIZE 64

static UART_HandleTypeDef *_huart;
static uint8_t rx_buf[LD2412_RX_BUF_SIZE];

// ใช้ volatile เพื่อป้องกันปัญหาการอ่านค่าจากตัวแปรที่ถูกอัปเดตผ่าน Interrupt
volatile uint8_t  g_ld2412_state       = LD2412_STATE_NONE;
volatile uint16_t g_ld2412_motion_dist = 0;
volatile uint16_t g_ld2412_static_dist = 0;

// ── ตัวนับ + สำเนาไบต์ดิบ เอาไว้ debug ──
#define LD2412_DBG_SIZE 32
static volatile uint32_t dbg_evt_count = 0;
static volatile uint32_t dbg_ok_count  = 0;
static volatile uint32_t dbg_rst_count = 0;
static volatile uint16_t dbg_last_size = 0;
static uint8_t  dbg_buf[LD2412_DBG_SIZE];

// ── รูปแบบเฟรม Basic Data ของ LD2412 (ยืนยันจากไบต์ดิบของจริงแล้ว) ──
//   +0..3   F4 F3 F2 F1        header
//   +4..5   0B 00              ความยาว data = 11 ไบต์
//   +6      0x02               data type = basic
//   +7      0xAA               head
//   +8      target state       0x00 none / 0x02 static / 0x03 motion+static
//   +9..10  moving distance    (cm, little endian)
//   +11     moving energy      ← ไบต์นี้เองที่โค้ดเดิมลืมนับ
//   +12..13 stationary distance(cm, little endian)
//   +14     stationary energy
//   +15..16 55 00              tail marker
//   +17..20 F8 F7 F6 F5        frame end
#define LD2412_FRAME_MIN  14   // ต้องอ่านได้ถึง offset +13

void LD2412_Init(UART_HandleTypeDef *huart) {
    _huart = huart;
    LD2412_Restart();
}

// เรียกซ้ำได้ตลอด ใช้ทั้งตอน init และตอนกู้ระบบหลัง UART error
void LD2412_Restart(void) {
    if (_huart == NULL) return;

    HAL_UART_AbortReceive(_huart);

    // เคลียร์ error flag ที่ค้างอยู่ ไม่งั้น HAL จะเด้ง error ซ้ำทันที
    __HAL_UART_CLEAR_OREFLAG(_huart);
    __HAL_UART_CLEAR_NEFLAG(_huart);
    __HAL_UART_CLEAR_FEFLAG(_huart);
    __HAL_UART_CLEAR_PEFLAG(_huart);

    // สั่งให้ DMA เริ่มรับข้อมูล และจะเรียก Interrupt เมื่อสาย UART ว่าง (IDLE)
    HAL_UARTEx_ReceiveToIdle_DMA(_huart, rx_buf, LD2412_RX_BUF_SIZE);

    // ปิดการแจ้งเตือนแบบครึ่งทาง (Half-Transfer) เพื่อไม่ให้รบกวน CPU บ่อยเกินไป
    __HAL_DMA_DISABLE_IT(_huart->hdmarx, DMA_IT_HT);

    dbg_rst_count++;
}

void LD2412_ProcessBuffer(uint16_t size) {
    if (size > LD2412_RX_BUF_SIZE) size = LD2412_RX_BUF_SIZE;

    // เก็บหลักฐานไว้ก่อนเสมอ ต่อให้ถอดรหัสไม่ผ่านก็ยังได้เห็นไบต์ดิบ
    dbg_evt_count++;
    dbg_last_size = size;
    uint16_t n = (size < LD2412_DBG_SIZE) ? size : LD2412_DBG_SIZE;
    memcpy(dbg_buf, rx_buf, n);
    if (n < LD2412_DBG_SIZE) memset(dbg_buf + n, 0, LD2412_DBG_SIZE - n);

    if (size < LD2412_FRAME_MIN) return;

    // DMA เป็นแบบ Circular ข้อมูลเก่าจะค้างอยู่ต้นบัฟเฟอร์
    // ถ้าไล่จากหน้าไปหลังแล้ว break ทันที จะได้เฟรมเก่าค้างตลอด
    // จึงไล่จากท้ายมาหน้า เพื่อให้ได้เฟรมล่าสุดเสมอ
    for (int i = (int)size - LD2412_FRAME_MIN; i >= 0; i--) {
        if (rx_buf[i]   == 0xF4 && rx_buf[i+1] == 0xF3 &&
            rx_buf[i+2] == 0xF2 && rx_buf[i+3] == 0xF1 &&
            rx_buf[i+6] == 0x02 && rx_buf[i+7] == 0xAA) {

            g_ld2412_state       = rx_buf[i+8];
            g_ld2412_motion_dist = (uint16_t)rx_buf[i+9]  | ((uint16_t)rx_buf[i+10] << 8);
            g_ld2412_static_dist = (uint16_t)rx_buf[i+12] | ((uint16_t)rx_buf[i+13] << 8);
            dbg_ok_count++;
            return;
        }
    }
}

void LD2412_GetLatest(uint8_t *presence_state, uint16_t *motion_dist_cm, uint16_t *static_dist_cm) {
    // ฟังก์ชันสำหรับให้ลูปหลักมาดึงค่าไปใช้แบบทันที (ไม่เกิดการรอใดๆ ทั้งสิ้น)
    *presence_state  = g_ld2412_state;
    *motion_dist_cm  = g_ld2412_motion_dist;
    *static_dist_cm  = g_ld2412_static_dist;
}

const char *LD2412_PresenceToString(uint8_t presence_state) {
    switch (presence_state) {
        case LD2412_STATE_NONE:          return "none";
        case LD2412_STATE_STATIC:        return "static";
        case LD2412_STATE_MOTION_STATIC: return "motion_static";
        default:                         return "unknown";
    }
}

uint32_t LD2412_GetEventCount(void) { return dbg_evt_count; }

void LD2412_GetDebug(uint32_t *evt, uint32_t *ok, uint32_t *rst, uint16_t *last_size,
                     uint8_t *buf_out, uint8_t buf_out_size) {
    *evt       = dbg_evt_count;
    *ok        = dbg_ok_count;
    *rst       = dbg_rst_count;
    *last_size = dbg_last_size;
    uint8_t n = (buf_out_size < LD2412_DBG_SIZE) ? buf_out_size : LD2412_DBG_SIZE;
    memcpy(buf_out, dbg_buf, n);
}
