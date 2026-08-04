![755a676192d6ef895ab2b64bd7178b83.png](../_resources/755a676192d6ef895ab2b64bd7178b83.png) ![de385b0bde0badf4ea53e66b92d01464.png](../_resources/de385b0bde0badf4ea53e66b92d01464.png)ccrx 是寄存器, 有效电平需要设置,

![ce0c68bab262897beee856a34f4bc458.png](../_resources/ce0c68bab262897beee856a34f4bc458.png)占空比: (有效高电平时间/总的时间)

![e44b96dd9244e841f2a18a52de777eb9.png](../_resources/e44b96dd9244e841f2a18a52de777eb9.png)修改低电平的时间可以决定led灯的亮度 ,所谓的低电平的持续时间就是修改占空比,也就是修改ccrx寄存器决定![14d299c1ec1dc425d869b198d245d352.png](../_resources/14d299c1ec1dc425d869b198d245d352.png)也就是修改pwmVal决定

![7d7ec4f4cdca2f76c91e1daf0ed202e8.png](../_resources/7d7ec4f4cdca2f76c91e1daf0ed202e8.png)对于Tclk=72M是72000000, Tout是溢出时间(周期), ![6227d86c0b21967216f447935178b21d.png](../_resources/6227d86c0b21967216f447935178b21d.png)

&nbsp;

### LED1 连接到哪个定时器的哪一路?

原理图: ![1aa127c729877f0f6643347371587c30.png](../_resources/1aa127c729877f0f6643347371587c30.png)  
产品手册:![c74030ff9ef238bcb2cbb6dfc4c551c2.png](../_resources/c74030ff9ef238bcb2cbb6dfc4c551c2.png)

第四个TIME的第三个通道的PWM

&nbsp;

&nbsp;

### 实战:

STM32CubeMX -- Access To -- STM32F103C8T6  
![247610b169bebfb341fe089de0b329fe.png](../_resources/247610b169bebfb341fe089de0b329fe.png)

设置外部时钟源rcc  
![32f3554b6f895c676c756b045fc779b8.png](../_resources/32f3554b6f895c676c756b045fc779b8.png)  
设置TIMER:

根据手册,设置timer4, 第3个通道

![6c6341ef6020d0996f9c68e31ad846e3.png](../_resources/6c6341ef6020d0996f9c68e31ad846e3.png)

CH Polarity: 设置为有效低电平 , Pulse: 占空比0(可以使用代码方式设置), Mode: PWM mode 1 设置模式1

![da2fbbeab2907eebcc40ec8d107daf0a.png](../_resources/da2fbbeab2907eebcc40ec8d107daf0a.png)

![40ad2a5fe7642cca6f57eeac89668ccf.png](../_resources/40ad2a5fe7642cca6f57eeac89668ccf.png)