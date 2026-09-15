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
#include "i2c.h"
#include "gpio.h"
#include "oled_picture.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

/* USER CODE BEGIN PV */
void Oled_Write_Cmd(uint8_t dataCmd) {
    HAL_I2C_Mem_Write(&hi2c1, 0x78, 0x00, I2C_MEMADD_SIZE_8BIT, 
                      &dataCmd, 1, 0xff); // 0x78是设备地址 1 发送数据的长度,1个字节 
}

void Oled_Write_Data(uint8_t dataData) {
    HAL_I2C_Mem_Write(&hi2c1, 0x78, 0x40, I2C_MEMADD_SIZE_8BIT, 
                      &dataData, 1, 0xff); // 0x78是设备地址 1 发送数据的长度,1个字节 
}

void Oled_Init(void)
{
    Oled_Write_Cmd(0xAE); //--display off
    Oled_Write_Cmd(0x00); //--set low column address
    Oled_Write_Cmd(0x10); //--set high column address
    Oled_Write_Cmd(0x40); //--set start line address
    Oled_Write_Cmd(0xB0); //--set page address
    Oled_Write_Cmd(0x81); // contract control
    Oled_Write_Cmd(0xFF); //--128
    Oled_Write_Cmd(0xA1); //set segment remap
    Oled_Write_Cmd(0xA6); //--normal / reverse
    Oled_Write_Cmd(0xA8); //--set multiplex ratio(1 to 64)
    Oled_Write_Cmd(0x3F); //--1/32 duty
    Oled_Write_Cmd(0xC8); //Com scan direction
    Oled_Write_Cmd(0xD3); //-set display offset
    Oled_Write_Cmd(0x00); //

    Oled_Write_Cmd(0xD5); //set osc division
    Oled_Write_Cmd(0x80); //

    Oled_Write_Cmd(0xD8); //set area color mode off
    Oled_Write_Cmd(0x05); //

    Oled_Write_Cmd(0xD9); //Set Pre‑Charge Period
    Oled_Write_Cmd(0xF1); //

    Oled_Write_Cmd(0xDA); //set com pin configuartion
    Oled_Write_Cmd(0x12); //

    Oled_Write_Cmd(0xDB); //set Vcomh
    Oled_Write_Cmd(0x30); //

    Oled_Write_Cmd(0x8D); //set charge pump enable
    Oled_Write_Cmd(0x14); //

    Oled_Write_Cmd(0xAF); //--turn on oled panel
}

void Oled_Clear(void)
{
    uint8_t page;
    uint8_t col;

    // 设置为页寻址模式
    Oled_Write_Cmd(0x20);
    Oled_Write_Cmd(0x02);

    // 8 页，每页 128 列，全部写 0
    for (page = 0; page < 8; page++)
    {
        Oled_Write_Cmd(0xB0 + page);
        Oled_Write_Cmd(0x00);  // 列地址低 4 位
        Oled_Write_Cmd(0x10);  // 列地址高 4 位

        for (col = 0; col < 128; col++)
        {
            Oled_Write_Data(0x00);
        }
    }

    // 写入位置恢复到第 0 页、第 0 列
    Oled_Write_Cmd(0xB0);
    Oled_Write_Cmd(0x00);
    Oled_Write_Cmd(0x10);
}

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
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
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */

HAL_Delay(100);
Oled_Init();
Oled_ShowPicture();

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

#ifdef  USE_FULL_ASSERT
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
