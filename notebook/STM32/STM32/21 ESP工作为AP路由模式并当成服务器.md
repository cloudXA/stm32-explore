# 21 ESP工作为AP路由模式并当成服务器
![alt text](image.png) 接线方式: ![alt text](image-1.png)
## 通过中断的方法获取接收到的数据(非中断方式用于调试)
- 使用2个串口,配置pb8, pb9 ![alt text](image-2.png), pb8, pb9默认拉高 ![alt text](image-3.png)
- uart2 串口是ch340, uart1 串口是82600 wifi模块
- 当uart1 发送入网LJWL时, 使用LJWL![alt text](image-4.png) 方式来判断入网状态;