/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : ESP8266 TCP server and LED control example
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
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/*
 * 命名说明：
 * ESP_LINE：ESP8266输出的一行AT应答，或接收中的+IPD头部。
 * DEBUG_TX：发往调试串口（USART2/CH340）的待发送数据。
 * BUF_LEN ：对应缓冲区可容纳的字节数。
 */
#define ESP_LINE_BUF_LEN         128U
#define DEBUG_TX_BUF_LEN         256U
#define ESP_INVALID_LINK_ID      0xFFU /* 不可能出现的连接ID，用于表示“无连接” */
#define ESP_MAX_LINK_ID          4U    /* CIPMUX=1时可用的连接ID为0~4 */
#define TCP_SERVER_PORT          8880U

/*
 * USART1连接ESP8266，USART2连接CH340调试串口。
 * ESP8266工作在Station模式，接入已有Wi-Fi后在8880端口监听TCP连接；
 * 上位机应连接“ESP8266获取到的IP地址:8880”。
 */
static const char WIFI_JOIN_COMMAND[] =
    "AT+CWJAP=\"mi\",\"12345678\"\r\n";
static const char ESP_RESET_COMMAND[] = "AT+RST\r\n";
static const char ESP_STATION_MODE_COMMAND[] = "AT+CWMODE=1\r\n";
static const char ESP_NORMAL_MODE_COMMAND[] = "AT+CIPMODE=0\r\n";
static const char ESP_MULTI_CONNECTION_COMMAND[] = "AT+CIPMUX=1\r\n";
static const char ESP_SERVER_START_COMMAND[] = "AT+CIPSERVER=1,8880\r\n";
static const char ESP_IP_QUERY_COMMAND[] = "AT+CIFSR\r\n";

/*
 * AT命令应答标志：由USART1接收中断置位，由主循环中的等待函数读取。
 * 这些变量在中断和主程序之间共享，因此必须使用volatile。
 */
volatile uint8_t WIFI_GOT_IP_Flag = 0U;
volatile uint8_t ESP_AT_OK_Flag = 0U;
volatile uint8_t ESP_AT_ERROR_Flag = 0U;
volatile uint8_t ESP_SEND_PROMPT_Flag = 0U;
volatile uint8_t ESP_SEND_OK_Flag = 0U;
volatile uint8_t ESP_SERVER_READY_Flag = 0U;

/* 当前TCP客户端状态；ESP8266多连接服务器模式支持连接ID 0~4。 */
volatile uint8_t TCP_CLIENT_CONNECTED = 0U;
volatile uint8_t TCP_CLIENT_ID = ESP_INVALID_LINK_ID;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t ESP_RX_BYTE = 0U;
/*
 * ESP_LINE_BUF：保存ESP8266当前正在输出的一行文本或+IPD头部。
 * ESP_LINE_LENGTH：缓冲区内已经接收、尚未完成解析的有效字节数。
 */
uint8_t ESP_LINE_BUF[ESP_LINE_BUF_LEN];
volatile uint16_t ESP_LINE_LENGTH = 0U;

/*
 * USART1 -> USART2转发环形缓冲区。
 * 接收中断负责写入，主循环负责发送；始终空出一个元素以区分“空”和“满”。
 * DEBUG_TX_HEAD：下一个待写入位置；DEBUG_TX_TAIL：下一个待发送位置。
 * DEBUG_TX_OVERRUN：写入速度过快导致缓冲区已满的标志。
 */
uint8_t DEBUG_TX_BUF[DEBUG_TX_BUF_LEN];
volatile uint16_t DEBUG_TX_HEAD = 0U;
volatile uint16_t DEBUG_TX_TAIL = 0U;
volatile uint8_t DEBUG_TX_OVERRUN = 0U;

/* +IPD数据区状态：记录当前TCP数据还剩多少字节，以及数据所属连接。 */
volatile uint16_t ESP_IPD_REMAINING = 0U;
volatile uint8_t ESP_IPD_LINK_ID = ESP_INVALID_LINK_ID;

/*
 * LED命令状态：0=无命令，1=开灯，2=关灯。
 * PENDING由接收中断置位，主循环取出后操作GPIO并回复客户端。
 */
volatile uint8_t LED_COMMAND_PENDING = 0U;
volatile uint8_t LED_COMMAND_LINK_ID = ESP_INVALID_LINK_ID;
uint8_t LED_COMMAND_MATCH = 0U;
uint8_t LED_PARSE_LINK_ID = ESP_INVALID_LINK_ID;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void UART2_DebugPrint(const char *message);
static void Debug_QueueData(const uint8_t *data, uint16_t length);
static void Debug_FlushPending(void);
static void LED_ParseByte(uint8_t link_id, uint8_t byte);
static void LED_ProcessPending(void);
static uint8_t ESP_SendCommandWaitOK(const char *command, uint32_t timeout_ms);
static uint8_t ESP_SendToClient(uint8_t link_id,
                                const uint8_t *data,
                                uint16_t length);
static uint8_t ESP_ParseIPDHeader(const uint8_t *header,
                                  uint16_t length,
                                  uint8_t *link_id,
                                  uint16_t *payload_length);
static void ESP_ProcessLine(char *line);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void UART2_DebugPrint(const char *message)
{
  /* 调试输出只在主程序上下文中调用，允许使用阻塞发送。 */
  if (message == NULL)
  {
    return;
  }

  (void)HAL_UART_Transmit(&huart2, (uint8_t *)message,
                          (uint16_t)strlen(message), 1000U);
}

static void Debug_QueueData(const uint8_t *data, uint16_t length)
{
  uint16_t i;

  /*
   * Queue表示“入队”：本函数由USART1接收中断调用，只将数据加入
   * DEBUG_TX_BUF，不在中断中执行可能阻塞的串口发送。
   */
  for (i = 0U; i < length; i++)
  {
    uint16_t next = (DEBUG_TX_HEAD + 1U) % DEBUG_TX_BUF_LEN;

    /* next追上TAIL表示缓冲区已满，保留已有数据并记录溢出。 */
    if (next == DEBUG_TX_TAIL)
    {
      DEBUG_TX_OVERRUN = 1U;
      break;
    }

    DEBUG_TX_BUF[DEBUG_TX_HEAD] = data[i];
    DEBUG_TX_HEAD = next;
  }
}

static void Debug_FlushPending(void)
{
  /*
   * FlushPending表示“发送仍在等待的数据”：由主程序将DEBUG_TX_BUF中
   * 尚未发送的数据通过USART2输出，每次发送一段连续内存以减少发送次数。
   */
  while (DEBUG_TX_TAIL != DEBUG_TX_HEAD)
  {
    uint16_t head = DEBUG_TX_HEAD;
    uint16_t length;

    if (head > DEBUG_TX_TAIL)
    {
      length = head - DEBUG_TX_TAIL;
    }
    else
    {
      length = DEBUG_TX_BUF_LEN - DEBUG_TX_TAIL;
    }

    if (HAL_UART_Transmit(&huart2,
                          &DEBUG_TX_BUF[DEBUG_TX_TAIL],
                          length, 1000U) != HAL_OK)
    {
      break;
    }

    DEBUG_TX_TAIL = (DEBUG_TX_TAIL + length) % DEBUG_TX_BUF_LEN;
  }
}

static void LED_ParseByte(uint8_t link_id, uint8_t byte)
{
  /* 客户端发生变化时清空匹配进度，避免拼接不同客户端的半条命令。 */
  if (LED_PARSE_LINK_ID != link_id)
  {
    LED_PARSE_LINK_ID = link_id;
    LED_COMMAND_MATCH = 0U;
  }

  if ((byte >= 'A') && (byte <= 'Z'))
  {
    /* 命令不区分大小写。 */
    byte = (uint8_t)(byte + ('a' - 'A'));
  }

  /*
   * 使用流式状态机匹配led1/led0，因此命令即使跨越多个TCP数据包也能识别。
   * 匹配到重复的'l'时保留状态1，便于从新的命令起点继续匹配。
   */
  switch (LED_COMMAND_MATCH)
  {
    case 0U:
      LED_COMMAND_MATCH = (byte == 'l') ? 1U : 0U;
      break;

    case 1U:
      LED_COMMAND_MATCH = (byte == 'e') ? 2U :
                          ((byte == 'l') ? 1U : 0U);
      break;

    case 2U:
      LED_COMMAND_MATCH = (byte == 'd') ? 3U :
                          ((byte == 'l') ? 1U : 0U);
      break;

    case 3U:
      if (byte == '1')
      {
        LED_COMMAND_LINK_ID = link_id;
        LED_COMMAND_PENDING = 1U;
      }
      else if (byte == '0')
      {
        LED_COMMAND_LINK_ID = link_id;
        LED_COMMAND_PENDING = 2U;
      }
      LED_COMMAND_MATCH = (byte == 'l') ? 1U : 0U;
      break;

    default:
      LED_COMMAND_MATCH = 0U;
      break;
  }
}

static uint8_t ESP_SendCommandWaitOK(const char *command, uint32_t timeout_ms)
{
  uint32_t start_tick;

  /* 清除上一次命令留下的结果，随后由接收中断解析OK/ERROR并置位。 */
  ESP_AT_OK_Flag = 0U;
  ESP_AT_ERROR_Flag = 0U;

  if (HAL_UART_Transmit(&huart1, (uint8_t *)command,
                        (uint16_t)strlen(command), 1000U) != HAL_OK)
  {
    return 0U;
  }

  start_tick = HAL_GetTick();
  while ((!ESP_AT_OK_Flag) && (!ESP_AT_ERROR_Flag) &&
         ((HAL_GetTick() - start_tick) < timeout_ms))
  {
    /* 等待AT应答期间继续排空调试缓冲区，避免接收数据堆积。 */
    Debug_FlushPending();
    HAL_Delay(1U);
  }

  Debug_FlushPending();
  return ESP_AT_OK_Flag ? 1U : 0U;
}

static uint8_t ESP_SendToClient(uint8_t link_id,
                                const uint8_t *data,
                                uint16_t length)
{
  char command[32];
  int command_length;
  uint32_t start_tick;

  if ((link_id > ESP_MAX_LINK_ID) || (data == NULL) || (length == 0U))
  {
    return 0U;
  }

  command_length = snprintf(command, sizeof(command),
                            "AT+CIPSEND=%u,%u\r\n",
                            (unsigned int)link_id,
                            (unsigned int)length);
  if ((command_length <= 0) ||
      ((uint32_t)command_length >= (uint32_t)sizeof(command)))
  {
    return 0U;
  }

  ESP_SEND_PROMPT_Flag = 0U;
  ESP_SEND_OK_Flag = 0U;
  ESP_AT_ERROR_Flag = 0U;

  if (HAL_UART_Transmit(&huart1, (uint8_t *)command,
                        (uint16_t)command_length, 1000U) != HAL_OK)
  {
    return 0U;
  }

  start_tick = HAL_GetTick();
  /* ESP8266返回'>'后才可以发送指定长度的数据。 */
  while ((!ESP_SEND_PROMPT_Flag) && (!ESP_AT_ERROR_Flag) &&
         ((HAL_GetTick() - start_tick) < 3000U))
  {
    Debug_FlushPending();
    HAL_Delay(1U);
  }

  if ((!ESP_SEND_PROMPT_Flag) || ESP_AT_ERROR_Flag)
  {
    return 0U;
  }

  if (HAL_UART_Transmit(&huart1, (uint8_t *)data, length, 1000U) != HAL_OK)
  {
    return 0U;
  }

  start_tick = HAL_GetTick();
  /* SEND OK表示数据已由ESP8266接受；超时或ERROR均视为发送失败。 */
  while ((!ESP_SEND_OK_Flag) && (!ESP_AT_ERROR_Flag) &&
         ((HAL_GetTick() - start_tick) < 5000U))
  {
    Debug_FlushPending();
    HAL_Delay(1U);
  }

  Debug_FlushPending();
  return ESP_SEND_OK_Flag ? 1U : 0U;
}

static void LED_ProcessPending(void)
{
  uint8_t command;
  uint8_t link_id;
  static const uint8_t LED_ON_REPLY[] = "LED ON\r\n";
  static const uint8_t LED_OFF_REPLY[] = "LED OFF\r\n";

  /* 必须等完整的+IPD数据区接收完毕，才能发送AT命令，避免收发状态互相干扰。 */
  if ((LED_COMMAND_PENDING == 0U) || (ESP_IPD_REMAINING != 0U))
  {
    return;
  }

  __disable_irq();
  /* 用短临界区原子地取出中断提交的命令，防止读取过程中被改写。 */
  command = LED_COMMAND_PENDING;
  link_id = LED_COMMAND_LINK_ID;
  LED_COMMAND_PENDING = 0U;
  __enable_irq();

  if (command == 1U)
  {
    /* 开发板LED为低电平点亮。 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
    UART2_DebugPrint("\r\n[GPIO] LED ON (PB8=LOW)\r\n");
    if (!ESP_SendToClient(link_id, LED_ON_REPLY,
                          (uint16_t)(sizeof(LED_ON_REPLY) - 1U)))
    {
      UART2_DebugPrint("[TCP] LED ON reply failed\r\n");
    }
  }
  else if (command == 2U)
  {
    /* PB8输出高电平时LED熄灭。 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
    UART2_DebugPrint("\r\n[GPIO] LED OFF (PB8=HIGH)\r\n");
    if (!ESP_SendToClient(link_id, LED_OFF_REPLY,
                          (uint16_t)(sizeof(LED_OFF_REPLY) - 1U)))
    {
      UART2_DebugPrint("[TCP] LED OFF reply failed\r\n");
    }
  }
}

static uint8_t ESP_ParseIPDHeader(const uint8_t *header,
                                  uint16_t length,
                                  uint8_t *link_id,
                                  uint16_t *payload_length)
{
  uint16_t pos;
  uint32_t value;

  /* 基本格式：+IPD,<连接ID>,<数据长度>:<数据> */
  if ((header == NULL) || (link_id == NULL) || (payload_length == NULL) ||
      (length < 9U) || (memcmp(header, "+IPD,", 5U) != 0) ||
      (header[length - 1U] != ':'))
  {
    return 0U;
  }

  pos = 5U;
  value = 0U;
  if ((pos >= length) || (header[pos] < '0') || (header[pos] > '9'))
  {
    return 0U;
  }
  while ((pos < length) && (header[pos] >= '0') && (header[pos] <= '9'))
  {
    value = (value * 10U) + (uint32_t)(header[pos] - '0');
    pos++;
  }
  if ((value > ESP_MAX_LINK_ID) || (pos >= length) || (header[pos] != ','))
  {
    return 0U;
  }
  *link_id = (uint8_t)value;

  pos++;
  value = 0U;
  if ((pos >= length) || (header[pos] < '0') || (header[pos] > '9'))
  {
    return 0U;
  }
  while ((pos < length) && (header[pos] >= '0') && (header[pos] <= '9'))
  {
    value = (value * 10U) + (uint32_t)(header[pos] - '0');
    if (value > 65535U)
    {
      return 0U;
    }
    pos++;
  }

  /* CIPDINFO=1时长度后可能附加远端IP和端口，但数据区仍以':'为起点。 */
  if ((pos >= length) ||
      ((header[pos] != ':') && (header[pos] != ',')))
  {
    return 0U;
  }

  *payload_length = (uint16_t)value;
  return 1U;
}

static void ESP_ProcessLine(char *line)
{
  if ((line == NULL) || (line[0] == '\0'))
  {
    return;
  }

  if (strstr(line, "WIFI GOT IP") != NULL)
  {
    /* 模块已从路由器获取IP地址。 */
    WIFI_GOT_IP_Flag = 1U;
  }

  /* 解析主程序中各等待函数所关心的通用AT应答。 */
  if (strcmp(line, "OK") == 0)
  {
    ESP_AT_OK_Flag = 1U;
  }
  else if (strcmp(line, "SEND OK") == 0)
  {
    ESP_SEND_OK_Flag = 1U;
  }
  else if ((strcmp(line, "ERROR") == 0) ||
           (strstr(line, "FAIL") != NULL))
  {
    ESP_AT_ERROR_Flag = 1U;
  }

  if ((line[0] >= '0') && (line[0] <= ('0' + ESP_MAX_LINK_ID)))
  {
    uint8_t link_id = (uint8_t)(line[0] - '0');

    /* 多连接模式下，连接事件格式为“<id>,CONNECT”或“<id>,CLOSED”。 */
    if (strcmp(&line[1], ",CONNECT") == 0)
    {
      TCP_CLIENT_ID = link_id;
      TCP_CLIENT_CONNECTED = 1U;
    }
    else if (strcmp(&line[1], ",CLOSED") == 0)
    {
      if (TCP_CLIENT_ID == link_id)
      {
        TCP_CLIENT_ID = ESP_INVALID_LINK_ID;
        TCP_CLIENT_CONNECTED = 0U;
      }
      if (LED_PARSE_LINK_ID == link_id)
      {
        LED_PARSE_LINK_ID = ESP_INVALID_LINK_ID;
        LED_COMMAND_MATCH = 0U;
      }
    }
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
  uint8_t wifi_connected = 0U;
  uint8_t server_started = 0U;
  uint32_t heartbeat_tick;
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
  /* USART1 <-> ESP8266；USART2 <-> CH340调试串口。 */
  HAL_Delay(300U);
  UART2_DebugPrint("\r\n=== ESP8266 TCP Server ===\r\n");
  UART2_DebugPrint("USART2: 115200, 8-N-1, TX=PA2, RX=PA3\r\n");

  if (HAL_UART_Receive_IT(&huart1, &ESP_RX_BYTE, 1U) != HAL_OK)
  {
    UART2_DebugPrint("ERROR: USART1 receive interrupt failed\r\n");
  }

  /*
   * 若ESP8266上次运行后仍处于透传模式，先用+++退出透传。
   * +++前后必须保留保护时间，在此期间不能发送其他数据。
   */
  UART2_DebugPrint("Resetting ESP8266...\r\n");
  HAL_Delay(1100U);
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)"+++", 3U, 1000U);
  HAL_Delay(1100U);
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2U, 1000U);
  HAL_Delay(100U);

  if (!ESP_SendCommandWaitOK(ESP_RESET_COMMAND, 3000U))
  {
    UART2_DebugPrint("Warning: ESP8266 reset did not return OK\r\n");
  }
  HAL_Delay(2500U);
  /* 复位后模块会输出启动信息，在继续配置前将其转发到调试串口。 */
  Debug_FlushPending();

  /* 配置顺序：Station模式 -> 连接Wi-Fi -> 普通模式 -> 多连接 -> TCP服务器。 */
  UART2_DebugPrint("Selecting WiFi station mode...\r\n");
  if (!ESP_SendCommandWaitOK(ESP_STATION_MODE_COMMAND, 3000U))
  {
    UART2_DebugPrint("ERROR: CWMODE failed\r\n");
  }
  else
  {
    UART2_DebugPrint("Joining WiFi...\r\n");
    WIFI_GOT_IP_Flag = 0U;
    if (ESP_SendCommandWaitOK(WIFI_JOIN_COMMAND, 30000U))
    {
      wifi_connected = 1U;
      UART2_DebugPrint("WiFi connected\r\n");
      UART2_DebugPrint("ESP8266 station IP follows:\r\n");
      (void)ESP_SendCommandWaitOK(ESP_IP_QUERY_COMMAND, 3000U);
    }
    else
    {
      UART2_DebugPrint("ERROR: WiFi join failed or timed out\r\n");
    }
  }

  if (wifi_connected)
  {
    UART2_DebugPrint("Selecting normal (non-transparent) mode...\r\n");
    if (!ESP_SendCommandWaitOK(ESP_NORMAL_MODE_COMMAND, 3000U))
    {
      UART2_DebugPrint("ERROR: CIPMODE=0 failed\r\n");
    }
    else
    {
      UART2_DebugPrint("Enabling multiple connections...\r\n");
      if (!ESP_SendCommandWaitOK(ESP_MULTI_CONNECTION_COMMAND, 3000U))
      {
        UART2_DebugPrint("ERROR: CIPMUX=1 failed\r\n");
      }
      else
      {
        UART2_DebugPrint("Starting TCP server on port 8880...\r\n");
        if (ESP_SendCommandWaitOK(ESP_SERVER_START_COMMAND, 3000U))
        {
          server_started = 1U;
          ESP_SERVER_READY_Flag = 1U;
          UART2_DebugPrint("TCP server ready: connect PC to ESP_IP:8880\r\n");
          UART2_DebugPrint("Commands: led1 = ON, led0 = OFF\r\n");
        }
        else
        {
          UART2_DebugPrint("ERROR: TCP server start failed\r\n");
        }
      }
    }
  }

  if (!server_started)
  {
    UART2_DebugPrint("Server is not ready; heartbeat starts now\r\n");
  }

  heartbeat_tick = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 耗时的GPIO控制、TCP回复和调试发送均放在主循环，不在中断中执行。 */
    LED_ProcessPending();
    Debug_FlushPending();

    if (DEBUG_TX_OVERRUN)
    {
      /* 清除与中断共享的溢出标志时使用短临界区。 */
      __disable_irq();
      DEBUG_TX_OVERRUN = 0U;
      __enable_irq();
      UART2_DebugPrint("\r\n[UART2] receive forwarding buffer overrun\r\n");
    }

    if ((!ESP_SERVER_READY_Flag) &&
        ((HAL_GetTick() - heartbeat_tick) >= 1000U))
    {
      /* 服务器启动失败时每秒输出一次心跳，表明MCU仍在运行。 */
      heartbeat_tick = HAL_GetTick();
      UART2_DebugPrint("[UART2] heartbeat\r\n");
    }
    HAL_Delay(1U);
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

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  /* 本应用只处理USART1（ESP8266）的逐字节接收完成中断。 */
  if (huart->Instance == USART1)
  {
    uint8_t byte = ESP_RX_BYTE;

    /*
     * +IPD数据区优先处理：数据内容可以合法包含'>'、回车或换行，
     * 因此不能再按AT应答字符解释。
     */
    if (ESP_IPD_REMAINING > 0U)
    {
      /* 只将TCP有效数据转发到USART2，并用于识别LED命令。 */
      LED_ParseByte(ESP_IPD_LINK_ID, byte);
      Debug_QueueData(&byte, 1U);
      ESP_IPD_REMAINING--;
      (void)HAL_UART_Receive_IT(&huart1, &ESP_RX_BYTE, 1U);
      return;
    }

    if (byte == '>')
    {
      /* CIPSEND命令的发送提示符不是普通的换行结尾应答。 */
      ESP_SEND_PROMPT_Flag = 1U;
      ESP_LINE_LENGTH = 0U;
      (void)HAL_UART_Receive_IT(&huart1, &ESP_RX_BYTE, 1U);
      return;
    }

    if ((byte == '\r') || (byte == '\n'))
    {
      uint16_t length = ESP_LINE_LENGTH;

      /* 收到行结束符后补NUL，将缓存作为C字符串解析。 */
      if (length > 0U)
      {
        ESP_LINE_BUF[length] = '\0';
        Debug_QueueData((const uint8_t *)"[ESP8266]: ", 11U);
        Debug_QueueData(ESP_LINE_BUF, length);
        Debug_QueueData((const uint8_t *)"\r\n", 2U);
        ESP_ProcessLine((char *)ESP_LINE_BUF);
      }

      ESP_LINE_LENGTH = 0U;
      (void)HAL_UART_Receive_IT(&huart1, &ESP_RX_BYTE, 1U);
      return;
    }

    if (ESP_LINE_LENGTH < (ESP_LINE_BUF_LEN - 1U))
    {
      uint8_t link_id;
      uint16_t payload_length;

      ESP_LINE_BUF[ESP_LINE_LENGTH] = byte;
      ESP_LINE_LENGTH++;

      if ((byte == ':') &&
          ESP_ParseIPDHeader(ESP_LINE_BUF, ESP_LINE_LENGTH,
                             &link_id, &payload_length))
      {
        /* 从下一个字节开始，严格按payload_length计数接收TCP数据区。 */
        ESP_IPD_LINK_ID = link_id;
        ESP_IPD_REMAINING = payload_length;
        TCP_CLIENT_ID = link_id;
        TCP_CLIENT_CONNECTED = 1U;
        ESP_LINE_LENGTH = 0U;
      }
    }
    else
    {
      /* 丢弃异常的超长AT应答，并从下一行重新同步。 */
      ESP_LINE_LENGTH = 0U;
    }

    (void)HAL_UART_Receive_IT(&huart1, &ESP_RX_BYTE, 1U);
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
  /* 发生不可恢复的HAL错误时关闭中断并停留在此处，便于调试定位。 */
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
