# STM32 串口 printf 重定向 & 回显 笔记

## 1. HAL_UART_Transmit 发送字符串

```c
// ❌ 硬编码长度，修改字符串时容易忘改
HAL_UART_Transmit(&huart1, (uint8_t *)"hello world\r\n", 13, 100);

// ✅ 用 strlen 自动计算长度
const char *msg = "hello world\r\n";
HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
```

> 参数：`(串口句柄, 数据指针, 长度, 超时ms)`
> `&huart1` 是取实体变量的地址（转成指针），`msg` 本身就是指针。

---

## 2. HAL_UART_Receive 接收并回显

### 逐字节回显（简单）
```c
uint8_t rx_byte;
while (1) {
    if (HAL_UART_Receive(&huart1, &rx_byte, 1, HAL_MAX_DELAY) == HAL_OK) {
        HAL_UART_Transmit(&huart1, &rx_byte, 1, 100);
    }
}
```

### 缓冲接收 + 换行处理
```c
char ch[64] = {0};
while (1) {
    HAL_UART_Receive(&huart1, (uint8_t *)ch, sizeof(ch) - 1, 100);
    printf(ch);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);  // 补换行
    memset(ch, 0, sizeof(ch));  // 彻底清零
}
```

### 更安全的版本（检查实际接收长度）
```c
char ch[64] = {0};
while (1) {
    uint16_t len = HAL_UART_Receive(&huart1, (uint8_t *)ch, sizeof(ch) - 1, 100);
    if (len > 0) {
        ch[len] = '\0';
        printf(ch);
    }
    memset(ch, 0, sizeof(ch));
}
```

---

## 3. printf 重定向到串口

### Keil + MicroLIB（AC5 编译器）
```c
/* 放在 main.c 的 USER CODE 0 区域 */
#include <stdio.h>

int fputc(int ch, FILE *f)
{
    unsigned char temp[1] = {ch};
    HAL_UART_Transmit(&huart1, temp, 1, 0xFFFF);
    return ch;
}
```

### STM32CubeIDE / GCC（newlib-nano）
```c
/* 放在 main.c 的 USER CODE 0 区域 */
#include <stdio.h>

int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
    return ch;
}

// 或者用 _write 更底层
int _write(int file, char *ptr, int len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, 0xFFFF);
    return len;
}
```

| 编译器 | 标准库 | printf 底层 | 重写函数 |
|--------|--------|-------------|----------|
| Keil AC5 | MicroLIB | fputc | `int fputc(int ch, FILE *f)` |
| GCC | newlib-nano | _write | `int __io_putchar(int ch)` 或 `int _write(...)` |

> **Keil 必须操作**：魔术棒 → Target → 勾选 **Use MicroLIB**

---

## 4. 常见问题

### 缓冲区太小导致截断
- 缓存大小要 > 预期最大输入长度
- 接收长度用 `sizeof(buf) - 1`，跟随缓冲区自动适配

### memset 只清 strlen，导致残留数据
```c
// ❌ 如果 receive 返回 0，strlen=0，memset 等于没清
memset(ch, 0, strlen(ch));

// ✅ 始终用 sizeof 彻底清零
memset(ch, 0, sizeof(ch));
```

### 输出没有换行
- Windows 下换行是 `\r\n`（回车 + 换行）
- 单独 `\n` 只换行不回行首
- 单独 `\r` 只回行首不换行

---

## 5. 最终完整模板（Keil 版）

```c
/* Includes */
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include <stdio.h>
#include <string.h>

/* printf 重定向 */
int fputc(int ch, FILE *f)
{
    unsigned char temp[1] = {ch};
    HAL_UART_Transmit(&huart1, temp, 1, 0xFFFF);
    return ch;
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    printf("hello world\r\n");

    while (1)
    {
        char ch[64] = {0};
        HAL_UART_Receive(&huart1, (uint8_t *)ch, sizeof(ch) - 1, HAL_MAX_DELAY);
        printf(ch);
        printf("\r\n");
        memset(ch, 0, sizeof(ch));
    }
}
```
