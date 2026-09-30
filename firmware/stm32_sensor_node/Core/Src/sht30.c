/*
 * sht30.c
 *
 *  Created on: 4 ก.ค. 2569
 *      Author: Phum
 */

#include "sht30.h"

// ── CRC-8 ตามสเปค SHT3x: polynomial 0x31, init 0xFF ──
static uint8_t sht30_crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

HAL_StatusTypeDef SHT30_Init(I2C_HandleTypeDef *hi2c) {
    // เช็คว่าคุยได้ไหม
    return HAL_I2C_IsDeviceReady(hi2c, SHT30_ADDR, 2, 100);
}

HAL_StatusTypeDef SHT30_Read(I2C_HandleTypeDef *hi2c,
                               float *temp,
                               float *hum) {
    uint8_t cmd[2] = {0x24, 0x00}; // High repeatability
    uint8_t buf[6];

    // ส่งคำสั่งวัด
    if (HAL_I2C_Master_Transmit(hi2c, SHT30_ADDR,
                                 cmd, 2, 100) != HAL_OK)
        return HAL_ERROR;

    HAL_Delay(20); // รอ measurement เสร็จ

    // รับข้อมูล 6 bytes
    // [0][1] = temp MSB/LSB
    // [2]    = temp CRC
    // [3][4] = hum MSB/LSB
    // [5]    = hum CRC
    if (HAL_I2C_Master_Receive(hi2c, SHT30_ADDR,
                                buf, 6, 100) != HAL_OK)
        return HAL_ERROR;

    // ── ตรวจ CRC ก่อนเชื่อค่า ──
    // ถ้าไม่ตรวจ แล้วบัสรวนจนได้ข้อมูลศูนย์ทั้งชุด จะแปลงได้ -45.0 C / 0.0 %
    // ซึ่งดูเหมือนค่าจริงแต่เป็นขยะ
    if (sht30_crc8(&buf[0], 2) != buf[2]) return HAL_ERROR;
    if (sht30_crc8(&buf[3], 2) != buf[5]) return HAL_ERROR;

    // แปลงค่า
    uint16_t raw_temp = (buf[0] << 8) | buf[1];
    uint16_t raw_hum  = (buf[3] << 8) | buf[4];

    *temp = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    *hum  = 100.0f * ((float)raw_hum  / 65535.0f);

    return HAL_OK;
}
