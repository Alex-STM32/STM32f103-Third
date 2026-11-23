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
#include <stdio.h>
#include "oled.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct __attribute__((packed)) {
  uint8_t  header;    /* 0xAA 帧头 */
  uint8_t  run;       /* 0=暂停 1=运行 */
  uint8_t  dir;       /* 0=正向 1=反向 */
  uint16_t step;      /* 切换间隔 ms（低字节在前）*/
  uint8_t  checksum;
} CmdPacket;            /* 共 6 字节 */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* ---- 运行状态 ---- */
uint8_t  run_flag   = 1;      /* 1=运行 0=暂停 */
uint8_t  dir_flag   = 0;      /* 0=正向 1=反向 */
uint16_t interval_ms = 300;   /* 切换间隔 */
uint8_t  led_index  = 0;      /* 当前点亮 0/1/2 */
uint32_t last_tick  = 0;

/* ---- 按键事件（中断置位，主循环清零）---- */
volatile uint8_t  dir_event   = 0;
volatile uint8_t  pause_event = 0;

/* ---- 按键消抖：每个按键各自记录上次被接受的时间 ---- */
volatile uint32_t last_dir_tick   = 0;
volatile uint32_t last_pause_tick = 0;

/* ---- 调试计数：记录每个按键"被接受"的中断次数（用于验证抖动是否已消除）---- */
volatile uint32_t dir_cnt   = 0;
volatile uint32_t pause_cnt = 0;

/* ---- 串口数据包 ---- */
CmdPacket        rx_packet;
volatile uint8_t packet_ready = 0;
uint8_t          feedback_req = 0;

/* ---- 串口反馈 ---- */
uint8_t          tx_msg[64];
volatile uint8_t tx_busy = 0;

/* ---- OLED 刷新标志（状态变化时置 1，主循环刷新后清 0）---- */
uint8_t          oled_dirty = 1;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  last_tick = HAL_GetTick();
  /* 启动串口接收中断，等待一整包 6 字节 */
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_packet, sizeof(rx_packet));

  /* 初始化 OLED（软件 I2C：PB0=SDA，PB1=SCL） */
  OLED_Init();
  OLED_Clear();

  /* 上电先把 LED0 点亮，和 OLED 显示的 LED 编号一致 */
  HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET);
  led_index = 0;
  oled_dirty = 1;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* ---- 1. 处理按键事件 ---- */
  if (dir_event) {
    dir_event = 0;
    dir_flag = !dir_flag;
    feedback_req = 1;
    oled_dirty = 1;
  }
  if (pause_event) {
    pause_event = 0;
    run_flag = !run_flag;
    feedback_req = 1;
    oled_dirty = 1;
  }

  /* ---- 2. 处理串口数据包 ---- */
  if (packet_ready) {
    CmdPacket p = rx_packet;
    packet_ready = 0;

    const uint8_t *b = (const uint8_t *)&p;
    uint8_t expected = (uint8_t)(b[0] + b[1] + b[2] + b[3] + b[4]);

    if (p.header == 0xAA && p.checksum == expected
        && p.run <= 1 && p.dir <= 1
        && p.step >= 100 && p.step <= 2000) {
      run_flag = p.run;
      dir_flag = p.dir;
      interval_ms = p.step;
      feedback_req = 1;
      oled_dirty = 1;
        }

    /* 重新开启接收，等下一包 */
    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_packet, sizeof(rx_packet));
  }

  /* ---- 3. 流水灯推进（非阻塞）---- */
  if (run_flag) {
    if (HAL_GetTick() - last_tick >= interval_ms) {
      last_tick = HAL_GetTick();

      if (dir_flag == 0) {
        led_index = (led_index + 1) % 3;       /* 正向 */
      } else {
        led_index = (led_index + 2) % 3;       /* 反向 */
      }

      HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET);
      if (led_index == 0)     HAL_GPIO_WritePin(LED0_GPIO_Port, LED0_Pin, GPIO_PIN_SET);
      else if (led_index == 1) HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);
      else                     HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);

      oled_dirty = 1;   /* LED 编号变化，OLED 里的 LED 编号要同步刷新 */
    }
  }

  /* ---- 4. OLED 状态刷新（只在状态变化时刷，避免占用总线）---- */
  if (oled_dirty) {
    char line[24];
    oled_dirty = 0;

    snprintf(line, sizeof(line), "STATE: %s", run_flag ? "RUN" : "PAUSE");
    OLED_ShowString(0, 0, line);

    snprintf(line, sizeof(line), "DIR: %s", dir_flag ? "REV" : "FWD");
    OLED_ShowString(0, 2, line);

    snprintf(line, sizeof(line), "STEP: %ums", (unsigned int)interval_ms);
    OLED_ShowString(0, 4, line);

    snprintf(line, sizeof(line), "LED: %u", (unsigned int)(led_index + 1));
    OLED_ShowString(0, 6, line);
  }

  /* ---- 5. 串口反馈（中断发送）---- */
  if (feedback_req && !tx_busy) {
    int n = snprintf((char *)tx_msg, sizeof(tx_msg),
        "STATE=%s,DIR=%s,STEP=%u,LED=%u\r\n",
        run_flag ? "RUN" : "PAUSE",
        dir_flag ? "REV" : "FWD",
        interval_ms, led_index + 1);
    if (n > 0 && HAL_UART_Transmit_IT(&huart1, tx_msg, (uint16_t)n) == HAL_OK) {
      tx_busy = 1;
      feedback_req = 0;
    }
  }

  /* USER CODE END 3 */
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

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

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LED0_Pin|LED1_Pin|LED2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LED0_Pin LED1_Pin LED2_Pin */
  GPIO_InitStruct.Pin = LED0_Pin|LED1_Pin|LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : KEY_DIR_Pin KEY_PAUSE_Pin */
  GPIO_InitStruct.Pin = KEY_DIR_Pin|KEY_PAUSE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* 按键外部中断回调（PA11 方向 / PA12 暂停）*/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  uint32_t now = HAL_GetTick();

  if (GPIO_Pin == GPIO_PIN_11) {
    if (now - last_dir_tick < 50) return;   /* 50ms 消抖：抖动产生的重复边沿直接丢弃 */
    last_dir_tick = now;
    dir_cnt++;
    dir_event = 1;
  } else if (GPIO_Pin == GPIO_PIN_12) {
    if (now - last_pause_tick < 50) return;
    last_pause_tick = now;
    pause_cnt++;
    pause_event = 1;
  }
}

/* 串口收满 6 字节完成回调 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) {
    packet_ready = 1;
  }
}

/* 串口发送完成回调 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) {
    tx_busy = 0;
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
