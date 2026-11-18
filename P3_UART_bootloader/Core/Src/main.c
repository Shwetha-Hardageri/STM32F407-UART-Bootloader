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
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_ADDR      0x08008000U

#define SOF           0xA5
#define ACK           0x79
#define NACK          0x1F
#define MAX_PAYLOAD   128
#define BL_VERSION    0x01
enum {
	CMD_GET_VER = 0x01,
	CMD_ERASE = 0x02,
	CMD_WRITE = 0x03,
	CMD_JUMP = 0x04
};
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint8_t rx[2 + MAX_PAYLOAD];   /* rx[0]=cmd, rx[1]=len, rx[2..]=payload */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static int app_is_valid(void)
{
    uint32_t msp = *(volatile uint32_t *)APP_ADDR;
    uint32_t rst = *(volatile uint32_t *)(APP_ADDR + 4U);
    return (msp >= 0x20000000U && msp <= 0x20020000U) &&
           (rst >= APP_ADDR && rst <= FLASH_END);
}

static void jump_to_app(void)
{
    uint32_t msp = *(volatile uint32_t *)APP_ADDR;
    uint32_t rst = *(volatile uint32_t *)(APP_ADDR + 4U);
    void (*app_reset)(void) = (void (*)(void))rst;

    HAL_UART_DeInit(&huart2);
    HAL_RCC_DeInit();
    HAL_DeInit();
    SysTick->CTRL = 0; SysTick->LOAD = 0; SysTick->VAL = 0;
    __disable_irq();
    for (int i = 0; i < 8; i++) { NVIC->ICER[i] = 0xFFFFFFFFU; NVIC->ICPR[i] = 0xFFFFFFFFU; }

    SCB->VTOR = APP_ADDR;     /* relocate vector table */
    __set_MSP(msp);           /* use the app's stack */
    __enable_irq();

    app_reset();              /* never returns */
}

/* Software CRC32, matches Python zlib.crc32 */
static uint32_t crc32_sw(const uint8_t *d, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFU;
    while (n--) {
        crc ^= *d++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)(-(int32_t)(crc & 1U)));
    }
    return ~crc;
}

static void send_byte(uint8_t b) { HAL_UART_Transmit(&huart2, &b, 1, 100); }

static void uart_flush(void)
{
    uint8_t d;
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    while (HAL_UART_Receive(&huart2, &d, 1, 20) == HAL_OK) { }
}

/* returns 0 on a good packet, -1 on error */
static int recv_packet(void)
{
    uint8_t b, c[4];

    do {                                   /* wait for start byte */
        HAL_UART_Receive(&huart2, &b, 1, HAL_MAX_DELAY);
    } while (b != SOF);

    if (HAL_UART_Receive(&huart2, rx, 2, 200) != HAL_OK) return -1;
    uint8_t len = rx[1];
    if (len > MAX_PAYLOAD) return -1;
    if (len && HAL_UART_Receive(&huart2, rx + 2, len, 500) != HAL_OK) return -1;
    if (HAL_UART_Receive(&huart2, c, 4, 200) != HAL_OK) return -1;

    uint32_t got = (uint32_t)c[0] | ((uint32_t)c[1] << 8) |
                   ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
    return (got == crc32_sw(rx, 2U + len)) ? 0 : -1;
}

static HAL_StatusTypeDef flash_erase_sectors(uint32_t first, uint32_t n)
{
    FLASH_EraseInitTypeDef e = {0};
    uint32_t err;
    e.TypeErase    = FLASH_TYPEERASE_SECTORS;
    e.Sector       = first;
    e.NbSectors    = n;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;      /* 2.7-3.6 V, word writes */
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef s = HAL_FLASHEx_Erase(&e, &err);
    HAL_FLASH_Lock();
    return s;
}

static HAL_StatusTypeDef flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef s = HAL_OK;
    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < len && s == HAL_OK; i += 4) {
        uint32_t w;
        memcpy(&w, data + i, 4);
        s = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, w);
    }
    HAL_FLASH_Lock();
    return s;
}

static void handle_cmd(void)
{
    uint8_t  cmd = rx[0], len = rx[1];
    uint8_t *p   = &rx[2];

    switch (cmd) {
    case CMD_GET_VER:
        send_byte(ACK); send_byte(BL_VERSION);
        break;

    case CMD_ERASE:
        if (len == 2 && p[1] > 0 && p[0] >= 2 && (p[0] + p[1]) <= 12 &&
            flash_erase_sectors(p[0], p[1]) == HAL_OK)
            send_byte(ACK);
        else
            send_byte(NACK);
        break;

    case CMD_WRITE: {
        uint32_t addr, n = (len >= 4) ? (uint32_t)(len - 4) : 0;
        if (len < 8 || (n & 3U)) { send_byte(NACK); break; }
        memcpy(&addr, p, 4);
        if ((addr & 3U) ||
            addr < APP_ADDR ||
            addr > FLASH_END ||
            n == 0U ||
            n > (FLASH_END - addr + 1U)) {
            send_byte(NACK);
            break;
        }
        send_byte(flash_write(addr, p + 4, n) == HAL_OK ? ACK : NACK);
        break;
    }

    case CMD_JUMP:
        if (app_is_valid()) {
            send_byte(ACK);
            HAL_Delay(20);          /* let the last byte leave the UART */
            NVIC_SystemReset();     /* clean boot, which jumps to the app */
        } else
            send_byte(NACK);
        break;

    default:
        send_byte(NACK);
    }
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
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  int force_bl = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET);

  if (app_is_valid() && !force_bl)
  {
      jump_to_app();
  }

  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (recv_packet() == 0)
    {
        handle_cmd();
    }
    else
    {
        uart_flush();
        send_byte(NACK);
        HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_14);
    }
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
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
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
