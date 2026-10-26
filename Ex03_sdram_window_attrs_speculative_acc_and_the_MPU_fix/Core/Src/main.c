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
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define REFRESH_COUNT     (1667)   /* from 3.5 - recompute if SDCLK changes */

#define SDRAM_MODEREG_BURST_LENGTH_1          ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   ((uint16_t)0x0000)
#define SDRAM_MODEREG_CAS_LATENCY_3           ((uint16_t)0x0030) // M4->1, M5->1
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE  ((uint16_t)0x0200) // M9->1


#define MODE        0                 /* 0 = default, 1 = Setup A, 2 = Setup B */
#define SDRAM_BASE  0xC0000000UL
#define TEST_WORDS  1024              /* 4 KB, fits in the 16 KB D-cache */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

UART_HandleTypeDef huart1;

SDRAM_HandleTypeDef hsdram1;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FMC_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/

static void SDRAM_Init_Sequence(SDRAM_HandleTypeDef *hsdram)
{
  FMC_SDRAM_CommandTypeDef Command;
  uint32_t tmpmrd;

  /* 1: clock enable */
  Command.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
  Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
  Command.AutoRefreshNumber      = 1;
  Command.ModeRegisterDefinition = 0;
  HAL_SDRAM_SendCommand(hsdram, &Command, 0xFFFF);
  HAL_Delay(1); /* >= 100us power-up wait, per datasheet init procedure */

  /* 2: precharge all banks */
  Command.CommandMode = FMC_SDRAM_CMD_PALL;
  HAL_SDRAM_SendCommand(hsdram, &Command, 0xFFFF);

  /* 3: auto-refresh x8 (>= 2 required by datasheet; 8 is the common safe margin) */
  Command.CommandMode       = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
  Command.AutoRefreshNumber = 8;
  HAL_SDRAM_SendCommand(hsdram, &Command, 0xFFFF);

  /* 4: load mode register 0000 0010 0011 0000*/
  tmpmrd = SDRAM_MODEREG_BURST_LENGTH_1 | SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
           SDRAM_MODEREG_CAS_LATENCY_3  | SDRAM_MODEREG_OPERATING_MODE_STANDARD |
           SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;
  Command.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
  Command.AutoRefreshNumber      = 1;
  Command.ModeRegisterDefinition = tmpmrd;
  HAL_SDRAM_SendCommand(hsdram, &Command, 0xFFFF);

  /* 5: refresh rate */
  HAL_SDRAM_ProgramRefreshRate(hsdram, REFRESH_COUNT);
}

int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
  return len;
}

/* USER CODE BEGIN 0 */

static void dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = 0xC5ACCE55;              /* unlock, needed on some Cortex-M7 revisions */
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void MPU_Config(void)
{
  MPU_Region_InitTypeDef r = {0};
  HAL_MPU_Disable();

  /* Region 0: the whole FMC SDRAM window, bank 1 and bank 2 (512 MB) */
  r.Enable           = MPU_REGION_ENABLE;
  r.Number           = MPU_REGION_NUMBER0;
  r.BaseAddress      = 0xC0000000UL;
  r.Size             = MPU_REGION_SIZE_512MB;
  r.SubRegionDisable = 0x00;
  r.AccessPermission = MPU_REGION_FULL_ACCESS;
#if MODE == 1
  /* Setup A: Normal, WB-RWA, executable (the risky setup) */
  r.TypeExtField = MPU_TEX_LEVEL1;
  r.IsShareable  = MPU_ACCESS_NOT_SHAREABLE;
  r.IsCacheable  = MPU_ACCESS_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_BUFFERABLE;
  r.DisableExec  = MPU_INSTRUCTION_ACCESS_ENABLE;
#else
  /* Setup B background: Device + XN */
  r.TypeExtField = MPU_TEX_LEVEL0;
  r.IsShareable  = MPU_ACCESS_SHAREABLE;
  r.IsCacheable  = MPU_ACCESS_NOT_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_BUFFERABLE;
  r.DisableExec  = MPU_INSTRUCTION_ACCESS_DISABLE;
#endif
  HAL_MPU_ConfigRegion(&r);

#if MODE == 2
  /* Region 1: only the populated SDRAM, Normal, WB-RWA, XN. Higher number wins. */
  r.Number       = MPU_REGION_NUMBER1;
  r.BaseAddress  = SDRAM_BASE;
  r.Size         = MPU_REGION_SIZE_16MB;      /* verify your SDRAM size */
  r.TypeExtField = MPU_TEX_LEVEL1;
  r.IsShareable  = MPU_ACCESS_NOT_SHAREABLE;
  r.IsCacheable  = MPU_ACCESS_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_BUFFERABLE;
  r.DisableExec  = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&r);
#endif

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static uint32_t read_pass(void)
{
  volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;
  uint32_t sum = 0;
  uint32_t t0 = DWT->CYCCNT;
  for (int i = 0; i < TEST_WORDS; i++) sum += p[i];
  uint32_t dt = DWT->CYCCNT - t0;
  (void)sum;
  return dt;
}

/* Shared by all fault handlers (called from stm32f7xx_it.c) */
void Fault_Report(const char *name)
{
  uint32_t cfsr = SCB->CFSR;
  printf("\r\n*** %s ***\r\nCFSR = 0x%08lX\r\n", name, cfsr);
  if (cfsr & (1UL << 24)) printf("UNALIGNED (UsageFault)\r\n");
  if (cfsr & (1UL << 17)) printf("INVSTATE (UsageFault)\r\n");
  if (cfsr & (1UL << 0))  printf("IACCVIOL (MemManage)\r\n");
  if (cfsr & (1UL << 1))  printf("DACCVIOL (MemManage)\r\n");
  if (cfsr & (1UL << 7))  printf("MMFAR = 0x%08lX\r\n", SCB->MMFAR);
  if (cfsr & (1UL << 8))  printf("IBUSERR (BusFault)\r\n");
  if (cfsr & (1UL << 9))  printf("PRECISERR (BusFault)\r\n");
  if (cfsr & (1UL << 10)) printf("IMPRECISERR (BusFault)\r\n");
  if (cfsr & (1UL << 15)) printf("BFAR = 0x%08lX\r\n", SCB->BFAR);
  HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_13, GPIO_PIN_SET);
}

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
  MX_FMC_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  setvbuf(stdout, NULL, _IONBF, 0);
  printf("\r\nExample 03, MODE %d\r\n", MODE);

  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;

  dwt_init();

#if MODE != 0
  MPU_Config();                         /* MPU first ... */
#endif
  SCB_EnableICache();                   /* ... then caches */
  SCB_EnableDCache();

  /* Fill the test area, then empty the cache so the first read is cold */
  volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;
  for (int i = 0; i < TEST_WORDS; i++) p[i] = i;
  SCB_CleanInvalidateDCache();

  uint32_t c1 = read_pass();            /* cold */
  uint32_t c2 = read_pass();            /* warm */
  printf("Read pass 1: %lu cycles\r\nRead pass 2: %lu cycles\r\n", c1, c2);

  printf("Unaligned 32-bit read at 0x%08lX ...\r\n", SDRAM_BASE + 1);
  volatile uint32_t u = *(volatile uint32_t *)(SDRAM_BASE + 1);
  printf("Unaligned read OK, value 0x%08lX\r\n", u);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 432;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
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
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/* FMC initialization function */
static void MX_FMC_Init(void)
{

  /* USER CODE BEGIN FMC_Init 0 */

  /* USER CODE END FMC_Init 0 */

  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /* USER CODE BEGIN FMC_Init 1 */

  /* USER CODE END FMC_Init 1 */

  /** Perform the SDRAM1 memory initialization sequence
  */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_32;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 8;
  SdramTiming.SelfRefreshTime = 5;
  SdramTiming.RowCycleDelay = 7;
  SdramTiming.WriteRecoveryTime = 3;
  SdramTiming.RPDelay = 2;
  SdramTiming.RCDDelay = 2;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler( );
  }

  /* USER CODE BEGIN FMC_Init 2 */
  SDRAM_Init_Sequence(&hsdram1);
  /* USER CODE END FMC_Init 2 */
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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOJ_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  /*Configure GPIO pin : PJ13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOJ, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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
