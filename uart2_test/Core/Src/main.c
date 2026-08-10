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
#define UART_RX_BUF_LEN 64
#define UART2_FORWARD_BUF_LEN 128

#define SIZE 12

/*
 * ESP8266 AT 指令区：USART1 是 STM32 与 ESP8266 之间的通道。
 * 进入透传模式前，STM32 用 AT 指令让 ESP8266 加入 Wi-Fi、连接 TCP Server。
 */
char buffer[SIZE];
char LJWL[]    = "AT+CWJAP=\"mi\",\"12345678\"\r\n";   //入网指令
// char LJFWQ[]   = "AT+CIPSTART=\"TCP\",\"192.168.1.10\",8880\r\n"; //连接服务器指令
char TCMS[]         = "AT+CIPMODE=1\r\n";                              //透传指令
char SJCS[]         = "AT+CIPSEND\r\n";                                //数据传输开始指令
char CJMK[]        = "AT+RST\r\n";                                    //重启模块指令
volatile uint8_t WIFI_GOT_IP_Flag = 0; /* 是否收到WIFI GOT IP, WIFI GOT IP返回值的标志位 */
volatile uint8_t WIFI_GOT_OK_Flag = 0; /* 是否收到OK, OK返回值的标志位 */

/* 中断收到 ESP8266 的关键回复后设置这些标志，主流程据此继续执行。 */
volatile uint8_t ESP_SEND_READY_Flag = 0;       /* 收到 '>'，可以开始发送数据 */
volatile uint8_t ESP_TRANSPARENT_MODE_Flag = 0;
/* TCP Server 在电脑上：IP 是电脑 Wi-Fi 地址，8880 是服务端监听端口。 */
static const char TCP_SERVER_COMMAND[] =
    "AT+CIPSTART=\"TCP\",\"10.154.61.205\",8880\r\n";
/* 查询 ESP8266 从 Wi-Fi 获取的局域网 IP，主要用于排查网络。 */
static const char ESP_IP_QUERY_COMMAND[] = "AT+CIFSR\r\n";

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t   UART_RX_BUF[UART_RX_BUF_LEN]; /* 接收缓冲区 */
volatile uint16_t UART_RX_STA = 0;      /* bit15=完�?, bit14-0=已收字节数 */

/*
 * USART1 -> USART2 转发环形缓冲区：
 * 接收中断只把网络数据快速放进缓冲区，主循环再经 CH340 发给串口助手，
 * 避免在中断中长时间发送而丢失 ESP8266 后续数据。
 */
uint8_t UART2_FORWARD_BUF[UART2_FORWARD_BUF_LEN];
volatile uint16_t UART2_FORWARD_HEAD = 0;
volatile uint16_t UART2_FORWARD_TAIL = 0;
volatile uint8_t UART2_FORWARD_OVERRUN = 0;
volatile uint8_t LED_COMMAND_PENDING = 0; /* 0=无命令，1=亮灯，2=灭灯 */
uint8_t LED_COMMAND_MATCH = 0;            /* 当前已匹配 led1/led0 的字符数 */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void UART2_DebugPrint(const char *message);
static void UART2_ForwardPending(void);
static void LED_ParseByte(uint8_t byte);
static void LED_ProcessPending(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void UART2_DebugPrint(const char *message)
{
  /* USART2 连接 CH340，本函数专门向电脑串口助手输出调试信息。 */
  if (message == NULL)
  {
    return;
  }

  (void)HAL_UART_Transmit(&huart2, (uint8_t *)message,
                          (uint16_t)strlen(message), 1000);
}

static void UART2_ForwardPending(void)
{
  /* 将环形缓冲区中尚未转发的数据分段发送到 USART2。 */
  while (UART2_FORWARD_TAIL != UART2_FORWARD_HEAD)
  {
    uint16_t head = UART2_FORWARD_HEAD;
    uint16_t length;

    if (head > UART2_FORWARD_TAIL)
    {
      length = head - UART2_FORWARD_TAIL;
    }
    else
    {
      length = UART2_FORWARD_BUF_LEN - UART2_FORWARD_TAIL;
    }

    if (HAL_UART_Transmit(&huart2,
                          &UART2_FORWARD_BUF[UART2_FORWARD_TAIL],
                          length, 1000) != HAL_OK)
    {
      break;
    }

    UART2_FORWARD_TAIL =
        (UART2_FORWARD_TAIL + length) % UART2_FORWARD_BUF_LEN;
  }
}

static void LED_ParseByte(uint8_t byte)
{
  /* 兼容 LED1、Led1 等写法，先统一转换为小写。 */
  if ((byte >= 'A') && (byte <= 'Z'))
  {
    byte = (uint8_t)(byte + ('a' - 'A'));
  }

  /*
   * 逐字节状态机：依次寻找 l -> e -> d -> 1/0。
   * TCP 是字节流，一条命令可能分多次到达，所以不能假设一次接收就是 led1。
   */
  switch (LED_COMMAND_MATCH)
  {
    case 0:
      LED_COMMAND_MATCH = (byte == 'l') ? 1U : 0U;
      break;

    case 1:
      LED_COMMAND_MATCH = (byte == 'e') ? 2U :
                          ((byte == 'l') ? 1U : 0U);
      break;

    case 2:
      LED_COMMAND_MATCH = (byte == 'd') ? 3U :
                          ((byte == 'l') ? 1U : 0U);
      break;

    case 3:
      if (byte == '1')
      {
        LED_COMMAND_PENDING = 1U;
      }
      else if (byte == '0')
      {
        LED_COMMAND_PENDING = 2U;
      }
      LED_COMMAND_MATCH = (byte == 'l') ? 1U : 0U;
      break;

    default:
      LED_COMMAND_MATCH = 0U;
      break;
  }
}

static void LED_ProcessPending(void)
{
  uint8_t command;

  /* pending 在接收中断中写入；短暂关中断保证“读取并清零”不可被打断。 */
  __disable_irq();
  command = LED_COMMAND_PENDING;
  LED_COMMAND_PENDING = 0U;
  __enable_irq();

  if (command == 1U)
  {
    /* 本开发板 PB8 的 LED 是低电平点亮。 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    UART2_DebugPrint("\r\n[GPIO] LED ON (PB8=LOW)\r\n");
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)"LED ON\r\n", 8U, 1000);
  }
  else if (command == 2U)
  {
    /* PB8 输出高电平时 LED 熄灭。 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    UART2_DebugPrint("\r\n[GPIO] LED OFF (PB8=HIGH)\r\n");
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)"LED OFF\r\n", 9U, 1000);
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
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /*
   * 两个串口的分工：
   * USART1 <-> ESP8266：发送 AT 指令，之后承载 TCP 透传数据。
   * USART2 <-> CH340 <-> 电脑：显示日志，帮助观察程序运行情况。
   */
  // uart2 串口是ch340, uart1 串口是8266 wifi模块
  HAL_Delay(300);
  UART2_DebugPrint("\r\n=== STM32 UART2 Ready ===\r\n");
  UART2_DebugPrint("USART2: 115200, 8-N-1, TX=PA2, RX=PA3\r\n");

  // 开启中断接收 ESP8266 的回复
  /* 每次接收 1 字节；完成后会进入 HAL_UART_RxCpltCallback。 */
  if (HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[0], 1) != HAL_OK)
  {
    UART2_DebugPrint("ERROR: USART1 receive interrupt failed\r\n");
  }

  // 1. 发入网指令
  UART2_DebugPrint("Joining WiFi...\r\n");
  WIFI_GOT_IP_Flag = 0;
  /* AT+CWJAP 让 ESP8266 连接指定的 Wi-Fi，与 TCP 连接是两个不同阶段。 */
  HAL_UART_Transmit(&huart1, (uint8_t *)LJWL, strlen(LJWL), 0xFFFF);

  // 等待 WiFi 连接成功（超时 10 秒）
  uint32_t t = HAL_GetTick();
  uint32_t status_tick = t;
  while (!WIFI_GOT_IP_Flag && (HAL_GetTick() - t) < 10000)
  {
    if ((HAL_GetTick() - status_tick) >= 1000U)
    {
      status_tick = HAL_GetTick();
      UART2_DebugPrint(".");
    }
  }
  UART2_DebugPrint("\r\n");
  if (WIFI_GOT_IP_Flag)
  {
    /* 获得 IP 只说明已经进入局域网，还没有连接电脑上的 TCP Server。 */
    UART2_DebugPrint("WiFi connected\r\n");
    UART2_DebugPrint("Reading ESP8266 IP address...\r\n");
    HAL_UART_Transmit(&huart1, (uint8_t *)ESP_IP_QUERY_COMMAND,
                      sizeof(ESP_IP_QUERY_COMMAND) - 1U, 0xFFFF);
    HAL_Delay(1000);
  }
  else
  {
    UART2_DebugPrint("WiFi timeout (check ESP8266 baud rate and wiring)\r\n");
  }
  WIFI_GOT_IP_Flag = 0;
  HAL_Delay(1000);

  // 2. 发连接服务器指令
  UART2_DebugPrint("Connecting TCP server...\r\n");
  WIFI_GOT_OK_Flag = 0;
  /* ESP8266 作为 TCP Client，主动连接电脑的 IP:8880。 */
  HAL_UART_Transmit(&huart1, (uint8_t *)TCP_SERVER_COMMAND,
                    sizeof(TCP_SERVER_COMMAND) - 1U, 0xFFFF);

  // 等待 TCP 连接成功（超时 10 秒）
  t = HAL_GetTick();
  status_tick = t;
  while (!WIFI_GOT_OK_Flag && (HAL_GetTick() - t) < 10000)
  {
    if ((HAL_GetTick() - status_tick) >= 1000U)
    {
      status_tick = HAL_GetTick();
      UART2_DebugPrint(".");
    }
  }
  UART2_DebugPrint("\r\n");
  uint8_t tcp_connected = 0;
  if (WIFI_GOT_OK_Flag)
  {
    tcp_connected = 1;
    UART2_DebugPrint("TCP connected\r\n");
  }
  else
  {
    UART2_DebugPrint("TCP timeout\r\n");
  }
  WIFI_GOT_OK_Flag = 0;
  HAL_Delay(1000);

  // 3. 发透传指令
  if (tcp_connected)
  {
    /*
     * 透明传输模式下，STM32 不再需要为每包数据发送 AT+CIPSEND。
     * USART1 写出的普通字节会被 ESP8266 送入 TCP，收到的 TCP 字节也原样回到 USART1。
     */
    UART2_DebugPrint("Enabling transparent mode...\r\n");
    WIFI_GOT_OK_Flag = 0;
    HAL_UART_Transmit(&huart1, (uint8_t *)TCMS, strlen(TCMS), 0xFFFF);

    t = HAL_GetTick();
    while (!WIFI_GOT_OK_Flag && (HAL_GetTick() - t) < 3000U)
    {
    }

    if (WIFI_GOT_OK_Flag)
    {
      ESP_SEND_READY_Flag = 0;
      ESP_TRANSPARENT_MODE_Flag = 0;
      UART2_DebugPrint("Waiting for CIPSEND prompt...\r\n");

  // 4. 发数据传输开始指令
      HAL_UART_Transmit(&huart1, (uint8_t *)SJCS, strlen(SJCS), 0xFFFF);

      t = HAL_GetTick();
      while (!ESP_SEND_READY_Flag && (HAL_GetTick() - t) < 5000U)
      {
      }

      if (ESP_SEND_READY_Flag)
      {
        /* 收到 '>' 后正式进入透传；网络助手发来的 led1/led0 可到达解析器。 */
        UART2_DebugPrint("Transparent receive ready; network data follows raw\r\n");
      }
      else
      {
        UART2_DebugPrint("CIPSEND prompt timeout\r\n");
      }
    }
    else
    {
      UART2_DebugPrint("CIPMODE command failed\r\n");
    }
  }
  else
  {
    UART2_DebugPrint("Transparent mode skipped because TCP is not connected\r\n");
  }

  if (!ESP_TRANSPARENT_MODE_Flag)
  {
    UART2_DebugPrint("Not connected; heartbeat starts now\r\n");
  }

  uint32_t heartbeat_tick = HAL_GetTick();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 中断负责收字节，主循环负责执行耗时的 GPIO、串口发送等业务。 */
    LED_ProcessPending();
    UART2_ForwardPending();

    if (!ESP_TRANSPARENT_MODE_Flag &&
        (HAL_GetTick() - heartbeat_tick) >= 1000U)
    {
      /* 未进入透传时每秒打印一次，证明 STM32 主循环仍在正常运行。 */
      heartbeat_tick = HAL_GetTick();
      UART2_DebugPrint("[UART2] heartbeat\r\n");
    }
    HAL_Delay(1);
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
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* 串�?�接收完�?中断回调 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  /* 本项目只在这里处理 USART1（ESP8266）的接收完成事件。 */
  if (huart->Instance == USART1)
  {
    if (ESP_TRANSPARENT_MODE_Flag)
    {
      /*
       * 透传阶段：当前字节来自 TCP Server。
       * 同一个字节同时送给 LED 命令解析器，并写入环形缓冲区供 USART2 显示。
       */
      uint16_t next =
          (UART2_FORWARD_HEAD + 1U) % UART2_FORWARD_BUF_LEN;

      LED_ParseByte(UART_RX_BUF[0]);

      if (next != UART2_FORWARD_TAIL)
      {
        UART2_FORWARD_BUF[UART2_FORWARD_HEAD] = UART_RX_BUF[0];
        UART2_FORWARD_HEAD = next;
      }
      else
      {
        /* head 的下一格追上 tail，说明缓冲区已满；保留旧数据并记录溢出。 */
        UART2_FORWARD_OVERRUN = 1;
      }

      /* 中断接收是一次性的，每处理完 1 字节都必须重新启动下一次接收。 */
      HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[0], 1);
      return;
    }

    /* 非透传阶段：把 AT 回复按行收集，等待 \r 或 \n。 */
    uint16_t idx = UART_RX_STA & 0x3FFF;

    if (UART_RX_BUF[idx] == '>')
    {
      /* AT+CIPSEND 返回 '>'，表示 ESP8266 已准备好接收透明传输数据。 */
      ESP_SEND_READY_Flag = 1;
      ESP_TRANSPARENT_MODE_Flag = 1;
      memset(UART_RX_BUF, 0, UART_RX_BUF_LEN);
      UART_RX_STA = 0;
      HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[0], 1);
      return;
    }

    /* \r 或 \n → 命令结�?� */
    if ((UART_RX_BUF[idx] == '\r') || (UART_RX_BUF[idx] == '\n'))
    {
      if (idx > 0)
      {
        UART_RX_BUF[idx] = '\0';

        // 转发到串口调试助手查看 ESP8266 回复了什么
        HAL_UART_Transmit(&huart2, (uint8_t *)"[ESP8266]: ",
                          strlen("[ESP8266]: "), 0xFFFF);
        HAL_UART_Transmit(&huart2, UART_RX_BUF, idx, 0xFFFF);
        HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2, 0xFFFF);

        /* 从完整的一行 AT 回复中提取主流程关心的状态。 */
        // 查看是否收到WIFI GOT IP
        if (strstr((char *)UART_RX_BUF, "WIFI GOT IP") != NULL) {
          WIFI_GOT_IP_Flag = 1;
        }
        if(strstr((char *)UART_RX_BUF, "OK") != NULL) {
          WIFI_GOT_OK_Flag = 1;
        }
        memset(UART_RX_BUF, 0, UART_RX_BUF_LEN);
        UART_RX_STA = 0;
        HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[0], 1);
      }
      else
      {
        /* 忽略空行，以�?� CRLF 中剩余的 LF。 */
        UART_RX_STA = 0;
        HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[0], 1);
      }
      return;
    }

    idx++;

    /*
     * 手机 BLE 工具通常�?会把输入框中的 "\\n" 转�?真正的�?�行符。
     * 本实验�?�有两个固定命令，因此收到完整命令�?�直接交给主循环处�?�。
     */
    if (idx >= UART_RX_BUF_LEN - 1)
    {
      /* 防止异常或过长的 AT 回复越界写入数组；丢弃本行后重新接收。 */
      memset(UART_RX_BUF, 0, UART_RX_BUF_LEN);
      UART_RX_STA = 0;
      HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[0], 1);
      return;
    }

    UART_RX_STA = idx;
    HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[idx], 1);
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
