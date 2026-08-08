# 1.使用stm32cubemx 生成初始化文件

1.  Pinout & Configuration > GPIO菜单  在 pinout view 选中PB8 PB9 下拉框 指定GPIO_Output, 并在GPIO 设置GPIO output level, 在SYS 菜单选择Serinal Write 可以重复修改;
2.  在Project Manager > Project 菜单 > Project Name , Project Location , Toolchain Folder Location , Toolchain/IDE 选择MKD-ARM 生成后默认打开; 在Code Generator 设置STM32Cube MCU packages and embedded software packs 下 copy only the necessary library files, Generated files 勾选Generate peripheral ..., Keep User Code..., Delete previously