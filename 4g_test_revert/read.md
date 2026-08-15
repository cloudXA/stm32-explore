先纠正一个关键点：换行符是 `\n`（反斜杠），不是 `/n`（斜杠）。

- 发送 `open\n`：表示发送 `open`，再发送一个换行字节。
- 发送 `open/n`：发送的是普通字符串 `"open/n"`，程序不会识别，会返回 `Unknown`。
- 大多数串口助手只需要输入 `open`，然后选择"发送新行"或按 Enter。

## 1. 程序启动时

初始化 GPIO 和 USART1：

```c
MX_GPIO_Init();
MX_USART1_UART_Init();
```

PB8 初始输出高电平：

```c
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
```

然后启动串口中断接收，每次只接收一个字节：

```c
HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[0], 1);
```

这里的 `1` 表示：收到一个字节后，就进入 `HAL_UART_RxCpltCallback()`。

---

## 2. 发送 `open\n`

实际发送的字节为：

| 字符 | ASCII 十六进制 |
|---|---:|
| `o` | `0x6F` |
| `p` | `0x70` |
| `e` | `0x65` |
| `n` | `0x6E` |
| `\n` | `0x0A` |

### 收到 `o`

第一个字节存入：

```c
UART_RX_BUF[0] = 'o';
```

进入接收回调，此时不是换行，所以：

```c
idx++;
UART_RX_STA = idx;
HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[idx], 1);
```

结果：

```text
UART_RX_BUF = "o"
UART_RX_STA = 1
```

并准备将下一个字节放入 `UART_RX_BUF[1]`。

### 收到 `p`、`e`、`n`

处理方式相同，最终状态为：

```text
UART_RX_BUF[0] = 'o'
UART_RX_BUF[1] = 'p'
UART_RX_BUF[2] = 'e'
UART_RX_BUF[3] = 'n'
UART_RX_STA = 4
```

### 收到 `\n`

换行符被存入：

```c
UART_RX_BUF[4] = '\n';
```

回调检测到命令结束：

```c
if ((UART_RX_BUF[idx] == '\r') ||
    (UART_RX_BUF[idx] == '\n'))
```

因为前面已经有4个字符，所以设置最高位，表示"一条命令接收完成"：

```c
UART_RX_STA = idx | 0x8000;
```

此时大约是：

```text
UART_RX_STA = 0x8004
```

其中：

- `0x8000`：命令接收完成标志
- `0x0004`：命令长度为4

---

## 3. 主循环处理 `open`

主循环不断检查：

```c
if (UART_RX_STA & 0x8000)
```

`0x8004 & 0x8000` 不等于0，所以开始处理命令。

先获取长度：

```c
uint16_t len = UART_RX_STA & 0x3FFF;
```

得到：

```text
len = 4
```

然后把原来的换行符位置改成字符串结束符：

```c
UART_RX_BUF[len] = '\0';
```

缓冲区从：

```text
o p e n \n
```

变成：

```text
o p e n \0
```

现在它就是一个标准C字符串 `"open"`。

接下来比较：

```c
if (strcmp((char *)UART_RX_BUF, "open") == 0)
```

比较结果相等，于是执行：

```c
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
```

PB8 输出低电平，GPIO开启，并返回：

```text
LED ON
```

最后清空缓冲区并重新开始接收：

```c
memset(UART_RX_BUF, 0, UART_RX_BUF_LEN);
UART_RX_STA = 0;
HAL_UART_Receive_IT(&huart1, &UART_RX_BUF[0], 1);
```

---

## 4. 发送 `close\n`

过程完全相同，只是命令长度变成5：

```text
c → UART_RX_BUF[0]
l → UART_RX_BUF[1]
o → UART_RX_BUF[2]
s → UART_RX_BUF[3]
e → UART_RX_BUF[4]
\n → UART_RX_BUF[5]
```

收到换行后：

```text
UART_RX_STA = 0x8005
```

主循环取得：

```text
len = 5
```

然后执行：

```c
UART_RX_BUF[5] = '\0';
```

缓冲区成为：

```text
"close"
```

第一个比较失败：

```c
strcmp(UART_RX_BUF, "open") != 0
```

第二个比较成功：

```c
strcmp(UART_RX_BUF, "close") == 0
```

于是执行：

```c
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
```

PB8 输出高电平，GPIO关闭，并返回：

```text
LED OFF
```

---

## 5. 如果串口助手发送的是 `\r\n`

很多串口助手按 Enter 后实际发送两个字符：

```text
\r\n
```

也就是：

```text
0x0D 0x0A
```

处理过程是：

1. 收到 `\r`，程序认为命令结束。
2. 主循环处理 `open` 或 `close`。
3. 重新启动串口接收。
4. 随后收到剩余的 `\n`。
5. 因为 `\n` 前没有命令内容，程序把它当作空行忽略。

所以现在的代码同时支持：

```text
open\n
open\r
open\r\n
```

## 总体流程

```text
串口助手发送字符
        ↓
USART1每收到一个字节触发中断
        ↓
字符依次放入 UART_RX_BUF
        ↓
遇到 \r 或 \n
        ↓
UART_RX_STA 设置接收完成标志
        ↓
主循环取得命令长度
        ↓
把换行位置修改为 \0
        ↓
strcmp 比较 open / close
        ↓
open  → PB8低电平
close → PB8高电平
        ↓
清空缓冲区并重新接收
```

这里属于"低电平有效"：`GPIO_PIN_RESET` 是开启，`GPIO_PIN_SET` 是关闭。
