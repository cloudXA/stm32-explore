# 1.使用stm32cubemx 生成初始化文件

1. 在 `Pinout & Configuration` 的芯片视图中选择 PB8、PB9，将其设为 `GPIO_Output`；再到 GPIO 参数中设置初始输出电平、输出模式、上下拉和速度。
2. 在 `System Core > SYS` 中把 `Debug` 设为 `Serial Wire`，这样保留 PA13（SWDIO）和 PA14（SWCLK）用于下载和调试。这里的 `Serial Wire` 不是串口通信。
3. 在 `Project Manager > Project` 中填写工程名与路径；若使用 Keil，`Toolchain / IDE` 选择 `MDK-ARM`。
4. 在 `Code Generator` 中可选择 `Copy only the necessary library files`，并勾选“为每个外设生成独立的 `.c/.h` 文件”和 `Keep User Code when re-generating`。

## 一次完整的生成流程

```text
选择芯片/开发板
→ 配置引脚和外设
→ 配置 RCC 与 Clock Configuration
→ 配置中断和 DMA（若需要）
→ 填写工程名并选择编译器
→ Generate Code
→ 编译、下载、观察现象
```

## 生成后先检查什么

1. `main.c` 中是否先调用 `HAL_Init()`、`SystemClock_Config()`，再调用各个 `MX_xxx_Init()`。
2. `gpio.c`、`usart.c`、`tim.c` 等文件是否生成，相关时钟和引脚是否正确。
3. 自己的代码尽量写在 `USER CODE BEGIN/END` 保护区内，避免重新生成时被覆盖。
4. 修改 `.ioc` 后重新生成，不要手工长期维护 CubeMX 自动生成区。
5. 编译成功不代表硬件一定正确；还要检查供电、共地、下载接口和引脚实际接线。

## 常见错误

- 把 `Serial Wire` 误认为 UART；它实际是 SWD 调试接口。
- 只配置引脚却忘记启用对应外设、中断或 DMA。
- 时钟树出现红色提示仍然生成代码。
- 把业务代码写在非用户保护区，重新生成后丢失。
- 工程路径包含过多特殊字符，导致旧版工具链找不到文件；遇到问题时可先换成短英文路径验证。
