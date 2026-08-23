![d7786ef20140004eecebbefc652accea.png](../_resources/d7786ef20140004eecebbefc652accea.png)
![cd9ed80a1c4fecaeb556c703204f88db.png](../_resources/cd9ed80a1c4fecaeb556c703204f88db.png)
![e8355cbff45942f619f44d35b5ee4ec6.png](../_resources/e8355cbff45942f619f44d35b5ee4ec6.png)
后续学习RTOS时，可把串口接收、AT解析、网络连接和业务处理拆成任务；当前先用中断加主循环理解完整数据流。
### wifi模块
![alt text](../_resources/image.png)
- 目标: 通过wifi模块,实现单片机的上网
- ![alt text](../_resources/image-1.png)
- ![alt text](../_resources/image-2.png)
- ![alt text](../_resources/image-3.png)
- ![alt text](../_resources/image-4.png)
- 官网说的115200是“对应官方ESP-AT固件的出厂配置”，不代表你手上模块当前保存的波特率一定是115200。

而且你第一张截图的历史命令中已经出现了：

```text
AT+UART=9600,8,1,0,0
```

在部分旧版ESP8266/NONOS-AT固件中，这条命令可能会把9600保存进Flash。如果当时恰好带了正确的`\r\n`并收到`OK`，很可能就是那次改的。也可能是卖家在出厂测试时已经设置为9600。

现在在9600下依次发送：

```text
AT+GMR
AT+UART_CUR?
AT+UART_DEF?
```

预期分别得到：

```text
AT version:...
SDK version:...
```

```text
+UART_CUR:9600,8,1,0,0
OK
```

```text
+UART_DEF:9600,8,1,0,0
OK
```

其中：

- `UART_CUR`：当前配置，不一定断电保存；
- `UART_DEF`：保存在Flash中的默认配置，断电后仍有效。

这是乐鑫官方对两条命令的定义。[ESP8266 ESP-AT命令文档](https://docs.espressif.com/projects/esp-at/en/release-v2.3.0.0_esp8266/AT_Command_Set/Basic_AT_Commands.html)

如果这些命令返回`ERROR`，说明可能是旧版AT固件，再尝试：

```text
AT+UART?
```

最简单的验证方式是：

1. 在9600下确认`AT`返回`OK`；
2. 彻底断电10秒；
3. 重新上电；
4. 仍然用9600发送`AT`。

断电后仍能返回`OK`，就说明9600已经是Flash中保存的配置，而不是串口助手偶然碰巧接收正确。

9600可以继续使用，没有问题，只是传输速度较慢。若想恢复115200，可在当前正常工作的9600下发送：

```text
AT+UART_DEF=115200,8,1,0,0
```

收到`OK`后关闭串口，改成115200再打开。如果是旧固件，则可能使用：

```text
AT+UART=115200,8,1,0,0
```

不建议直接执行`AT+RESTORE`，因为它除了恢复波特率，还会清除已保存的Wi-Fi及其他AT配置。

- ![alt text](../_resources/3036b7060b05e7ecf1b1b71641576028.jpg)
- ![alt text](../_resources/cfa8e0773ca990c99a4c95cac794c241.jpg)
- ![alt text](../_resources/image-5.png)

## ESP8266在系统中的位置

STM32F103本身没有Wi-Fi射频和TCP/IP协议栈，ESP8266承担联网工作：

```text
STM32业务代码
↕ UART字节
ESP8266的AT固件
↕ Wi-Fi与TCP/IP
无线路由器/网络服务器
```

STM32通过UART发送AT命令配置模块；建立网络连接后，再发送或接收业务数据。AT命令和 `led1` 等业务协议不是同一层。

## 最小手工调试顺序

每条AT命令通常以 `\r\n` 结束，并等待完整响应后再发送下一条：

```text
AT                  → OK
AT+GMR              → 查看固件版本和命令集
AT+CWMODE=1         → STA模式
AT+CWJAP="ssid","password"
AT+CIFSR            → 查看本机IP
```

不同ESP-AT/NONOS-AT固件的命令名、返回文本和保存行为可能不同。先用 `AT+GMR` 确认固件，再查对应版本文档；不要把某个教程的命令无条件复制到所有模块。

## 硬件注意

- ESP8266芯片和GPIO通常使用3.3V逻辑，不能把STM32串口直接接到真正的5V UART信号。
- Wi-Fi发射会产生较大瞬时电流，供电不足常表现为反复复位、乱码、入网后掉线。电源应留足余量并在模块附近放置去耦电容。
- TX/RX交叉且必须共地；EN/CH_PD、RST和启动脚要处于正确状态。
- 模块上电启动日志的波特率可能与AT命令波特率不同，看到一小段乱码不一定表示AT串口参数错误。

## 程序不要靠固定延时猜结果

更可靠的结构是：

```text
发送一条AT命令
→ USART中断/DMA持续收字节
→ 按行或状态机解析 OK/ERROR/WIFI GOT IP/CONNECT/CLOSED
→ 成功进入下一状态，失败则超时重试或恢复
```

每个等待都要有超时；接收缓冲区要处理溢出；不能假设一条AT回复会在一次串口回调中完整到达。


