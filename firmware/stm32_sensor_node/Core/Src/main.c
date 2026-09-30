/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7735.h"
#include "fonts.h"
#include "sht30.h"
#include "bh1750.h"
#include "ds3231.h"
#include "ld2412.h"
#include "uart_tx.h"
#include "display.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart2_rx;

/* USER CODE BEGIN PV */
SensorData_t  g_data    = {0};
DS3231_Time_t rtc       = {0};
char          disp_buf[32];

uint8_t  light_on = 0;
uint8_t  wifi_ok  = 0;

// Timing
uint32_t last_periodic = 0;   // ส่งทุก 30 วิ
#define PERIODIC_MS    30000

// Event tracking
uint8_t  prev_motion = 0;
float    prev_lux    = 0;


uint32_t last_sensor = 0;
uint32_t last_disp   = 0;
uint32_t last_uart   = 0;

uint8_t display_on    = 1;
uint8_t light_manual  = 0;  // manual override

// Debounce
uint32_t btn1_last = 0;
uint32_t btn2_last = 0;
#define DEBOUNCE_MS 200
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */
void I2C_RecoverBus(I2C_HandleTypeDef *hi2c);
float Read_MCU_Temperature(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
volatile uint8_t actual_light_state = 0; // สถานะไฟที่แท้จริง (0=OFF, 1=ON)

// ── เพิ่มชุดตัวแปรรับข้อมูลแบบทีละไบต์ ──
uint8_t rx1_byte;
char rx1_buffer[32];
uint8_t rx1_index = 0;
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  ST7735_Init();
      ST7735_FillScreen(ST7735_BLACK);
      ST7735_WriteString(5, 5, "Initializing...",
                         Font_7x10, ST7735_WHITE, ST7735_BLACK);

      // Init SHT30
      if (SHT30_Init(&hi2c1) == HAL_OK)
          ST7735_WriteString(5, 20, "SHT30  OK",
                             Font_7x10, ST7735_GREEN, ST7735_BLACK);
      else
          ST7735_WriteString(5, 20, "SHT30  FAIL",
                             Font_7x10, ST7735_RED, ST7735_BLACK);

      // Init BH1750
      if (BH1750_Init(&hi2c1) == HAL_OK)
          ST7735_WriteString(5, 32, "BH1750 OK",
                             Font_7x10, ST7735_GREEN, ST7735_BLACK);
      else
          ST7735_WriteString(5, 32, "BH1750 FAIL",
                             Font_7x10, ST7735_RED, ST7735_BLACK);

      // Init DS3231
      if (DS3231_Init(&hi2c1) == HAL_OK)
          ST7735_WriteString(5, 44, "DS3231 OK",
                             Font_7x10, ST7735_GREEN, ST7735_BLACK);
      else
          ST7735_WriteString(5, 44, "DS3231 FAIL",
                             Font_7x10, ST7735_RED, ST7735_BLACK);

      // Init LD2412 (polling-based, no DMA/interrupt)
      LD2412_Init(&huart2);
      ST7735_WriteString(5, 56, "LD2412 OK",
                         Font_7x10, ST7735_GREEN, ST7735_BLACK);

      HAL_Delay(2000);
      HAL_UART_Receive_IT(&huart1, &rx1_byte, 1);
      ST7735_FillScreen(ST7735_BLACK);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
      	uint8_t i2c_error_count = 0;
        // ── สร้างตัวแปรจับเวลาแยกสำหรับแต่ละงาน (Soft Timer) ──
        // ตั้งค่าเริ่มต้นให้เวลาเหลื่อมกันเล็กน้อย ป้องกันการแย่งบัส I2C ในวินาทีแรก
        uint32_t last_bh1750 = 0;
        uint32_t last_sht30  = 200;
        uint32_t last_ds3231 = 400;
        uint32_t last_disp   = 800;

        // ตัวแปรสำหรับจับ Event (ค่าเปลี่ยน)
        uint8_t prev_presence_state = LD2412_STATE_NONE;
        uint16_t prev_lux = 0;

        uint8_t  pb0_raw = 1, pb0_last = 1, pb0_stable = 1;
		uint32_t pb0_timer = 0;
		uint8_t  pb1_raw = 1, pb1_last = 1, pb1_stable = 1;
		uint32_t pb1_timer = 0;
		uint32_t last_motion_disp_time = 0;

		// เฝ้าดูว่าเรดาร์ยังส่งข้อมูลเข้ามาอยู่ไหม ถ้าเงียบเกิน 3 วิให้กู้การรับ
		uint32_t ld_last_evt      = 0;
		uint32_t ld_last_evt_time = 0;
		uint32_t last_raw_dbg = 0;   // DEBUG เรดาร์ ลบทิ้งได้เมื่อแก้ปัญหาจบ

        while (1)
        {
            uint32_t now = HAL_GetTick();
			  // ════════════════════════════════════════════════════════════
			  // ── 1. สวิตช์จอ (PB0 - สวิตช์เลื่อนค้าง) ──
			  //    ขาเป็น PULLUP: สวิตช์ปิดวงจร = อ่านได้ 0 = เปิดจอ
			  // ════════════════════════════════════════════════════════════
			  pb0_raw = (HAL_GPIO_ReadPin(BTN_DISPLAY_GPIO_Port, BTN_DISPLAY_Pin) == GPIO_PIN_RESET) ? 0 : 1;
			  if (pb0_raw != pb0_last) { pb0_timer = now; pb0_last = pb0_raw; }
			  if ((now - pb0_timer) > 50) pb0_stable = pb0_raw;

			  // รีเซ็ตเวลาจอเมื่อเรดาร์เจอคน (เผื่อเปิดใช้ auto-sleep ทีหลัง)
			  if (g_data.motion == 1) {
			  	last_motion_disp_time = now;
			  }
			  (void)last_motion_disp_time;

			  uint8_t target_disp = (pb0_stable == 0) ? 1 : 0;

			  // ถ้าสถานะจอเปลี่ยน ค่อยสั่งอัปเดตหรือเคลียร์จอ
			  if (target_disp != display_on) {
			  	display_on = target_disp;
			  	if (display_on) {
			  		Display_Update(&g_data, &rtc, actual_light_state, wifi_ok, i2c_error_count);
			  	} else {
			  		ST7735_FillScreen(ST7735_BLACK);
			  	}
			  }

			  // ════════════════════════════════════════════════════════════
			  // ── 2. ปุ่มไฟ (PB1 - ปุ่มกด) กดติด-กดดับ ──
			  // ════════════════════════════════════════════════════════════
			  pb1_raw = (HAL_GPIO_ReadPin(SW_LIGHT_GPIO_Port, SW_LIGHT_Pin) == GPIO_PIN_RESET) ? 0 : 1;

			  if (pb1_raw != pb1_last) {
			  	pb1_timer = now;
			  	pb1_last = pb1_raw;
			  }

			  if ((now - pb1_timer) > 20) {
			  	if (pb1_raw != pb1_stable) {
			  		pb1_stable = pb1_raw;

			  		// Edge Detection: ทำงานเฉพาะตอน "เริ่มกดลงไป"
			  		if (pb1_stable == 0) {
			  			if (actual_light_state == 1) {
			  				actual_light_state = 0;
			  				UART_TX_SendCmd(&huart1, "LIGHT_OFF");
			  			} else {
			  				actual_light_state = 1;
			  				UART_TX_SendCmd(&huart1, "LIGHT_ON");
			  			}
			  			UART_TX_SendJSON(&huart1, &g_data);
			  			// ไม่เรียก Display_Update() ตรงนี้ เพื่อไม่ให้ MCU ค้าง
			  			// หลอกตัวแปรเวลาให้ลูปถัดไปไปอัปเดตจอเอง
			  			last_disp = 0;
			  		}
			  	}
			  }

			  // ── 3. อ่านเซนเซอร์แบบจัดคิว พร้อมระบบ Auto-Reset (I2C Watchdog) ──
			  HAL_StatusTypeDef status = HAL_OK;

			  // 3.1 อ่านเซนเซอร์แสง (BH1750) ทุกๆ 500ms
			  if (now - last_bh1750 >= 500) {
				  last_bh1750 = now;
				  // ถ้าฟังก์ชันของคุณไม่มี return HAL_Status ก็ไม่เป็นไร แต่ถ้ามีให้ดักจับแบบนี้ครับ
				  // สมมติว่าทุกฟังก์ชันคืนค่า HAL_OK เมื่อสำเร็จ
				  status = BH1750_Read(&hi2c1, &g_data.lux);
				  if (status != HAL_OK) i2c_error_count++; else i2c_error_count = 0;
			  }

			  // 3.2 อ่านเซนเซอร์อุณหภูมิ/ความชื้น (SHT30) ทุกๆ 2000ms
			  if (now - last_sht30 >= 2000) {
				  last_sht30 = now;
				  status = SHT30_Read(&hi2c1, &g_data.temperature, &g_data.humidity);
				  if (status != HAL_OK) i2c_error_count++; else i2c_error_count = 0;

				  g_data.stm32_temp = Read_MCU_Temperature();
			  }

			  // 3.3 อ่านเวลา (DS3231) ทุกๆ 1000ms
			  if (now - last_ds3231 >= 1000) {
				  last_ds3231 = now;
				  status = DS3231_GetTime(&hi2c1, &rtc);
				  if (status == HAL_OK) {
					  DS3231_ToString(&rtc, g_data.timestamp);
					  i2c_error_count = 0;
				  } else {
					  i2c_error_count++;
				  }
			  }

			  // ── ระบบ Auto-Recovery (ทำงานเมื่อ I2C แฮงก์ติดกัน 3 ครั้ง) ──
			  if (i2c_error_count >= 3) {
				  i2c_error_count = 0;
				  I2C_RecoverBus(&hi2c1); // กระชากบัสและรีสตาร์ทเซนเซอร์
			  }

            // ── 4. อัปเดตข้อมูลจาก LD2412 (ดึงจาก RAM ทันที, มาจาก DMA IDLE callback) ──

            // Watchdog: เรดาร์ส่งเฟรมมาราวๆ 10 ครั้ง/วินาที ถ้าเงียบเกิน 3 วิ
            // แปลว่าการรับตายแล้ว (error callback อาจไม่ทันได้ยิงด้วยซ้ำ) ให้กู้เอง
            {
                uint32_t evt_now = LD2412_GetEventCount();
                if (evt_now != ld_last_evt) {
                    ld_last_evt = evt_now;
                    ld_last_evt_time = now;
                } else if (ld_last_evt_time != 0 && (now - ld_last_evt_time) > 3000) {
                    ld_last_evt_time = now;
                    LD2412_Restart();
                }
                if (ld_last_evt_time == 0) ld_last_evt_time = now;
            }

            LD2412_GetLatest(&g_data.presence_state, &g_data.motion_dist_cm, &g_data.static_dist_cm);
            g_data.motion = (g_data.presence_state != LD2412_STATE_NONE) ? 1 : 0;

            // ── 5. อัปเดตหน้าจอทุกๆ 1 วินาที (ถ้าเปิดจออยู่) ──
            if (display_on && (now - last_disp >= 1000)) {
                last_disp = now;
                Display_Update(&g_data, &rtc, actual_light_state, wifi_ok, i2c_error_count);
            }

            // ── DEBUG: ส่งไบต์ดิบของเรดาร์ทุก 3 วิ ให้ ESP32 พิมพ์ออกจอ ──
            //    เมื่อแก้ปัญหา LD2412 เสร็จแล้ว ลบบล็อกนี้ทิ้งได้เลย
            if (now - last_raw_dbg >= 3000) {
                last_raw_dbg = now;
                UART_TX_SendRadarDebug(&huart1);
            }

            // ── 6. ส่งข้อมูล JSON ไป ESP32 ──

            // Event 1: เมื่อสถานะ Presence เปลี่ยน (ครอบคลุมทั้งมี/ไม่มีคน และ static<->motion+static)
            if (g_data.presence_state != prev_presence_state) {
                prev_presence_state = g_data.presence_state;
                UART_TX_SendJSON(&huart1, &g_data);
            }

            // Event 2: เมื่อแสงเปลี่ยนแบบข้ามเกณฑ์ (Threshold)
            // สมมติว่าตั้ง LUX_THRESHOLD ไว้ที่ 50
            uint16_t LUX_THRESHOLD = 50;
            bool was_dark = (prev_lux < LUX_THRESHOLD);
            bool is_dark  = (g_data.lux < LUX_THRESHOLD);
            if (was_dark != is_dark) {
                prev_lux = g_data.lux;
                UART_TX_SendJSON(&huart1, &g_data);
            }

            // Periodic: ส่งอัปเดตสถานะทุกๆ 30 วินาที
            int ts_len = strlen(g_data.timestamp);
			if (ts_len >= 5) {
				static char last_sent_min = 'X';
				char current_min = g_data.timestamp[ts_len-4]; // ดึงหลักหน่วยของนาที
				char sec_tens = g_data.timestamp[ts_len-2];
				char sec_units = g_data.timestamp[ts_len-1];

				// ถ้าข้อความเวลาลงท้ายด้วย "00" และนาทีไม่ซ้ำกับรอบที่แล้ว
				if (sec_tens == '0' && sec_units == '0') {
					if (current_min != last_sent_min) {
						last_sent_min = current_min;
						UART_TX_SendJSON(&huart1, &g_data);
					}
				}
			}
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
  /* DMA1_Channel6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel6_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel6_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, TFT_RST_Pin|TFT_DC_Pin|TFT_CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : TFT_RST_Pin TFT_DC_Pin TFT_CS_Pin */
  GPIO_InitStruct.Pin = TFT_RST_Pin|TFT_DC_Pin|TFT_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_DISPLAY_Pin SW_LIGHT_Pin */
  GPIO_InitStruct.Pin = BTN_DISPLAY_Pin|SW_LIGHT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
float Read_MCU_Temperature(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, HAL_MAX_DELAY);

    uint32_t adc = HAL_ADC_GetValue(&hadc1);

    HAL_ADC_Stop(&hadc1);

    float Vsense = (adc * 3.3f) / 4095.0f;

    // ค่า Typical ของ STM32F103
    float Temp = ((1.43f - Vsense) / 0.0043f) + 25.0f;

    return Temp;
}

// ── ฟังก์ชันกระชากบัส I2C ให้หลุดจากสถานะ Lockup (9-Clock Pulse) ──
void I2C_RecoverBus(I2C_HandleTypeDef *hi2c) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. ปิดการทำงานของ I2C Hardware เดิม
    __HAL_I2C_DISABLE(hi2c);
    HAL_I2C_DeInit(hi2c);

    // 2. ตั้งค่าขา PB6 (SCL) และ PB7 (SDA) เป็น Output Open-Drain ชั่วคราว
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 3. ปล่อยสาย SDA เป็น HIGH แล้วปั๊มสัญญาณ SCL สูงสุด 9 ครั้งเพื่อบังคับให้ Slave คายสาย
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    for (int i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        HAL_Delay(1);

        // ถ้าเซนเซอร์ยอมปล่อยสาย SDA ให้เป็น HIGH แล้ว ถือว่ากู้สำเร็จ (ออกลูปได้เลย)
        if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET) {
            break;
        }
    }

    // 4. สร้างสัญญาณ STOP Condition (SCL HIGH -> SDA พุ่งจาก LOW ไป HIGH)
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    HAL_Delay(1);

    // 5. คืนชีพ I2C กลับมาใช้งานตามปกติ
    MX_I2C1_Init();

    // 6. ส่งคำสั่ง Init เซนเซอร์ใหม่ทั้งหมด
    SHT30_Init(hi2c);
    BH1750_Init(hi2c);
    DS3231_Init(hi2c);
}

// ── ฟังก์ชันนี้ให้ DMA ของเรดาร์ (LD2412) ใช้อย่างเดียว ──
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2) {
        LD2412_ProcessBuffer(Size);
    }
}

// ── ถ้า UART เจอ error (ส่วนใหญ่คือ overrun) HAL จะหยุดรับข้อมูลถาวร ──
// ไม่มีใครสั่งเริ่มใหม่ = เรดาร์เงียบไปเลยจนกว่าจะรีเซ็ตบอร์ด
// อาการที่เจอคือ evt/ok ค้างนิ่ง แล้วสถานะ presence ค้างค่าสุดท้ายไว้
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        LD2412_Restart();
    }
    else if (huart->Instance == USART1) {
        // ช่องคุยกับ ESP32 ก็ต้องกู้เหมือนกัน ไม่งั้นจะรับคำสั่งไม่ได้อีกเลย
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        HAL_UART_Receive_IT(&huart1, &rx1_byte, 1);
    }
}

// ── เพิ่มฟังก์ชันนี้ให้ ESP32 คอยต่อจิ๊กซอว์ตัวอักษร ──
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {

        // ถ้าเจอตัวตัดขึ้นบรรทัดใหม่ (\n)
        if (rx1_byte == '\n' || rx1_index >= 31) {
            rx1_buffer[rx1_index] = '\0'; // ปิดท้ายข้อความ

            // ถอดรหัสคำสั่ง
            if (strstr(rx1_buffer, "STATE:ON") != NULL) {
                actual_light_state = 1;
            } else if (strstr(rx1_buffer, "STATE:OFF") != NULL) {
                actual_light_state = 0;
            }
            // ── [เพิ่มใหม่] รับสถานะ WiFi ──
            else if (strstr(rx1_buffer, "WIFI:ON") != NULL) {
                wifi_ok = 1;
            } else if (strstr(rx1_buffer, "WIFI:OFF") != NULL) {
                wifi_ok = 0;
            }

            rx1_index = 0;

        } else {
            rx1_buffer[rx1_index++] = rx1_byte;
        }

        HAL_UART_Receive_IT(&huart1, &rx1_byte, 1);
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
