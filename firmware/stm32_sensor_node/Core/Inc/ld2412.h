#ifndef LD2412_H
#define LD2412_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

// Raw target-state byte values (frame offset 8)
#define LD2412_STATE_NONE          0x00
#define LD2412_STATE_STATIC        0x02
#define LD2412_STATE_MOTION_STATIC 0x03

void LD2412_Init(UART_HandleTypeDef *huart);
void LD2412_Restart(void);          // เริ่มรับใหม่หลัง UART error / ข้อมูลเงียบ
void LD2412_ProcessBuffer(uint16_t size);
void LD2412_GetLatest(uint8_t *presence_state, uint16_t *motion_dist_cm, uint16_t *static_dist_cm);
const char *LD2412_PresenceToString(uint8_t presence_state);

// ── ของไว้ debug: ดูว่าเรดาร์ส่งอะไรมาจริงๆ ──
//   evt = จำนวนครั้งที่ UART มีข้อมูลเข้ามา
//   ok  = จำนวนครั้งที่ถอดรหัสเฟรมสำเร็จ
//   rst = จำนวนครั้งที่ต้องกู้การรับข้อมูลกลับมา
uint32_t LD2412_GetEventCount(void);
void LD2412_GetDebug(uint32_t *evt, uint32_t *ok, uint32_t *rst, uint16_t *last_size,
                     uint8_t *buf_out, uint8_t buf_out_size);

#endif
