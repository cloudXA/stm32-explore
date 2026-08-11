/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : ESP8266 TCP server example
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
#define UART_RX_BUF_LEN          128U
#define UART2_FORWARD_BUF_LEN    256U
#define ESP_INVALID_LINK_ID      0xFFU
#define ESP_MAX_LINK_ID          4U
#define TCP_SERVER_PORT          8880U

/*
 * USART1 connects the STM32 to the ESP8266.
 * The ESP8266 remains a Wi-Fi station, joins the existing access point, and
 * listens as a TCP server. The PC connects to the ESP8266 station IP:8880.
 */
static const char WIFI_JOIN_COMMAND[] =
    "AT+CWJAP=\"mi\",\"12345678\"\r\n";
static const char ESP_RESET_COMMAND[] = "AT+RST\r\n";
static const char ESP_STATION_MODE_COMMAND[] = "AT+CWMODE=1\r\n";
static const char ESP_NORMAL_MODE_COMMAND[] = "AT+CIPMODE=0\r\n";
static const char ESP_MULTI_CONNECTION_COMMAND[] = "AT+CIPMUX=1\r\n";
static const char ESP_SERVER_START_COMMAND[] = "AT+CIPSERVER=1,8880\r\n";
static const char ESP_IP_QUERY_COMMAND[] = "AT+CIFSR\r\n";

/* AT command/result flags written by the USART1 receive interrupt. */
volatile uint8_t WIFI_GOT_IP_Flag = 0U;
volatile uint8_t ESP_AT_OK_Flag = 0U;
volatile uint8_t ESP_AT_ERROR_Flag = 0U;
volatile uint8_t ESP_SEND_PROMPT_Flag = 0U;
volatile uint8_t ESP_SEND_OK_Flag = 0U;
volatile uint8_t ESP_SERVER_READY_Flag = 0U;

/* Current TCP client. ESP8266 server mode supports link IDs 0..4. */
volatile uint8_t TCP_CLIENT_CONNECTED = 0U;
volatile uint8_t TCP_CLIENT_ID = ESP_INVALID_LINK_ID;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t ESP_RX_BYTE = 0U;
uint8_t UART_RX_BUF[UART_RX_BUF_LEN];
volatile uint16_t UART_RX_STA = 0U;

/* USART1 -> USART2 forwarding ring buffer. */
uint8_t UART2_FORWARD_BUF[UART2_FORWARD_BUF_LEN];
volatile uint16_t UART2_FORWARD_HEAD = 0U;
volatile uint16_t UART2_FORWARD_TAIL = 0U;
volatile uint8_t UART2_FORWARD_OVERRUN = 0U;

/* +IPD payload state. */
volatile uint16_t ESP_IPD_REMAINING = 0U;
volatile uint8_t ESP_IPD_LINK_ID = ESP_INVALID_LINK_ID;

/* LED command state. 1=LED on, 2=LED off. */
volatile uint8_t LED_COMMAND_PENDING = 0U;
volatile uint8_t LED_COMMAND_LINK_ID = ESP_INVALID_LINK_ID;
uint8_t LED_COMMAND_MATCH = 0U;
uint8_t LED_PARSE_LINK_ID = ESP_INVALID_LINK_ID;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void UART2_DebugPrint(const char *message);
static void UART2_QueueData(const uint8_t *data, uint16_t length);
static void UART2_ForwardPending(void);
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
  if (message == NULL)
  {
    return;
  }

  (void)HAL_UART_Transmit(&huart2, (uint8_t *)message,
                          (uint16_t)strlen(message), 1000U);
}

static void UART2_QueueData(const uint8_t *data, uint16_t length)
{
  uint16_t i;

  for (i = 0U; i < length; i++)
  {
    uint16_t next = (UART2_FORWARD_HEAD + 1U) % UART2_FORWARD_BUF_LEN;

    if (next == UART2_FORWARD_TAIL)
    {
      UART2_FORWARD_OVERRUN = 1U;
      break;
    }

    UART2_FORWARD_BUF[UART2_FORWARD_HEAD] = data[i];
    UART2_FORWARD_HEAD = next;
  }
}

static void UART2_ForwardPending(void)
{
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
                          length, 1000U) != HAL_OK)
    {
      break;
    }

    UART2_FORWARD_TAIL =
        (UART2_FORWARD_TAIL + length) % UART2_FORWARD_BUF_LEN;
  }
}

static void LED_ParseByte(uint8_t link_id, uint8_t byte)
{
  /* Do not combine partial commands from two different TCP clients. */
  if (LED_PARSE_LINK_ID != link_id)
  {
    LED_PARSE_LINK_ID = link_id;
    LED_COMMAND_MATCH = 0U;
  }

  if ((byte >= 'A') && (byte <= 'Z'))
  {
    byte = (uint8_t)(byte + ('a' - 'A'));
  }

  /* Streaming matcher for led1 / led0; commands may span TCP packets. */
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
    UART2_ForwardPending();
    HAL_Delay(1U);
  }

  UART2_ForwardPending();
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
  while ((!ESP_SEND_PROMPT_Flag) && (!ESP_AT_ERROR_Flag) &&
         ((HAL_GetTick() - start_tick) < 3000U))
  {
    UART2_ForwardPending();
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
  while ((!ESP_SEND_OK_Flag) && (!ESP_AT_ERROR_Flag) &&
         ((HAL_GetTick() - start_tick) < 5000U))
  {
    UART2_ForwardPending();
    HAL_Delay(1U);
  }

  UART2_ForwardPending();
  return ESP_SEND_OK_Flag ? 1U : 0U;
}

static void LED_ProcessPending(void)
{
  uint8_t command;
  uint8_t link_id;
  static const uint8_t LED_ON_REPLY[] = "LED ON\r\n";
  static const uint8_t LED_OFF_REPLY[] = "LED OFF\r\n";

  /* Wait until the complete +IPD payload has arrived before transmitting AT. */
  if ((LED_COMMAND_PENDING == 0U) || (ESP_IPD_REMAINING != 0U))
  {
    return;
  }

  __disable_irq();
  command = LED_COMMAND_PENDING;
  link_id = LED_COMMAND_LINK_ID;
  LED_COMMAND_PENDING = 0U;
  __enable_irq();

  if (command == 1U)
  {
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

  /* Expected: +IPD,<link id>,<length>: */
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

  /* CIPDINFO=1 may append remote IP/port; the payload still starts at ':'. */
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
    WIFI_GOT_IP_Flag = 1U;
  }

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
  /* USART1 <-> ESP8266; USART2 <-> CH340 debug serial port. */
  HAL_Delay(300U);
  UART2_DebugPrint("\r\n=== ESP8266 TCP Server ===\r\n");
  UART2_DebugPrint("USART2: 115200, 8-N-1, TX=PA2, RX=PA3\r\n");

  if (HAL_UART_Receive_IT(&huart1, &ESP_RX_BYTE, 1U) != HAL_OK)
  {
    UART2_DebugPrint("ERROR: USART1 receive interrupt failed\r\n");
  }

  /*
   * Recover if the ESP8266 was left in transparent mode by an earlier run.
   * Guard time is required on both sides of the +++ escape sequence.
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
  UART2_ForwardPending();

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
    LED_ProcessPending();
    UART2_ForwardPending();

    if (UART2_FORWARD_OVERRUN)
    {
      __disable_irq();
      UART2_FORWARD_OVERRUN = 0U;
      __enable_irq();
      UART2_DebugPrint("\r\n[UART2] receive forwarding buffer overrun\r\n");
    }

    if ((!ESP_SERVER_READY_Flag) &&
        ((HAL_GetTick() - heartbeat_tick) >= 1000U))
    {
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
  if (huart->Instance == USART1)
  {
    uint8_t byte = ESP_RX_BYTE;

    if (ESP_IPD_REMAINING > 0U)
    {
      /* Only TCP payload bytes are forwarded and parsed as LED commands. */
      LED_ParseByte(ESP_IPD_LINK_ID, byte);
      UART2_QueueData(&byte, 1U);
      ESP_IPD_REMAINING--;
      (void)HAL_UART_Receive_IT(&huart1, &ESP_RX_BYTE, 1U);
      return;
    }

    if (byte == '>')
    {
      ESP_SEND_PROMPT_Flag = 1U;
      UART_RX_STA = 0U;
      (void)HAL_UART_Receive_IT(&huart1, &ESP_RX_BYTE, 1U);
      return;
    }

    if ((byte == '\r') || (byte == '\n'))
    {
      uint16_t length = UART_RX_STA;

      if (length > 0U)
      {
        UART_RX_BUF[length] = '\0';
        UART2_QueueData((const uint8_t *)"[ESP8266]: ", 11U);
        UART2_QueueData(UART_RX_BUF, length);
        UART2_QueueData((const uint8_t *)"\r\n", 2U);
        ESP_ProcessLine((char *)UART_RX_BUF);
      }

      UART_RX_STA = 0U;
      (void)HAL_UART_Receive_IT(&huart1, &ESP_RX_BYTE, 1U);
      return;
    }

    if (UART_RX_STA < (UART_RX_BUF_LEN - 1U))
    {
      uint8_t link_id;
      uint16_t payload_length;

      UART_RX_BUF[UART_RX_STA] = byte;
      UART_RX_STA++;

      if ((byte == ':') &&
          ESP_ParseIPDHeader(UART_RX_BUF, UART_RX_STA,
                             &link_id, &payload_length))
      {
        ESP_IPD_LINK_ID = link_id;
        ESP_IPD_REMAINING = payload_length;
        TCP_CLIENT_ID = link_id;
        TCP_CLIENT_CONNECTED = 1U;
        UART_RX_STA = 0U;
      }
    }
    else
    {
      /* Drop an abnormal overlong AT response and start a fresh line. */
      UART_RX_STA = 0U;
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
