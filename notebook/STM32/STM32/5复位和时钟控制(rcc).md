# 复位和时钟控制(rcc)

## 复位

1.  系统复位
2.  电源复位
3.  备份域复位

## 时钟

GPIO打开是由时钟控制的,时钟打开,对应设备才能工作,包括其他的外设

### 时钟来源

```
1. HSI 振荡器时钟：高速内部RC时钟，不需要外部晶振
2. HSE 振荡器时钟：高速外部时钟；CubeMX中的 `Crystal/Ceramic Resonator` 指外部晶体/陶瓷谐振器接法
3. PLL 时钟 锁相环倍频时钟
```

#### 使用cubx控制时钟

![f640fbeb41050bcc4c73d1f19e483af6.png](../_resources/f640fbeb41050bcc4c73d1f19e483af6.png)  
![74aae50c4f236c2626b21e868298501b.png](../_resources/74aae50c4f236c2626b21e868298501b.png)  
时钟相关的代码

```c
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
```

## 这段配置如何得到72MHz

假设开发板外部晶振为8MHz：

```text
HSE 8MHz
→ PLL倍频 ×9
→ SYSCLK 72MHz
→ AHB不分频：HCLK 72MHz
→ APB1二分频：PCLK1 36MHz
→ APB2不分频：PCLK2 72MHz
```

STM32F103的APB1最高通常为36MHz，因此这里必须二分频。还要注意：当APB预分频不为1时，该APB上的多数定时器时钟通常是PCLK的2倍，所以PCLK1为36MHz时，TIM2/3/4的定时器输入时钟可以是72MHz。

## 复位与时钟的联系

- 复位让CPU、外设寄存器和程序流程回到规定初始状态，但不同复位类型影响范围不同。
- RCC不仅选择系统时钟，还负责给GPIO、USART、定时器等外设“开门”。外设时钟未使能时，配置寄存器通常不会按预期工作。
- 调试频率问题时，先看时钟源，再看PLL，再看AHB/APB分频，最后看具体外设是否还有额外分频或倍频规则。

## 验证步骤

1. CubeMX时钟树无红色错误。
2. 检查 `SystemCoreClock` 是否为预期值。
3. 用MCO输出时钟或用定时器翻转GPIO并用示波器/逻辑分析仪测量。
4. 串口乱码时除波特率外，也要检查USART外设时钟是否算错。
