# 21 ESP8266联网实验（本页当前内容实际为STA + TCP Client）
![alt text](image.png) 接线方式: ![alt text](image-1.png)
## 通过中断的方法获取接收到的数据(非中断方式用于调试)
- 使用2个串口,配置pb8, pb9 ![alt text](image-2.png), pb8, pb9默认拉高 ![alt text](image-3.png)
- USART2连接CH340作为调试口，USART1连接ESP8266 Wi-Fi模块。
- USART1发送入网命令后，应根据ESP8266实际返回的 `WIFI CONNECTED`、`WIFI GOT IP`、`OK` 或 `ERROR` 更新状态；不要假设整段返回会一次到达。
- 当uart1 发送入网LJWL时, 使用LJWL![alt text](image-4.png) 方式来判断入网状态;
- 上一行保留原截图和原记录；其中 `LJWL` 若是旧代码中的匹配字符串/变量名，应结合截图与实际固件返回重新命名，业务判断仍以完整AT响应状态机为准。
- 本页当前流程是ESP8266作为TCP Client，主动连接PC上的TCP Server。

## 先纠正标题中的角色混淆

以下四个角色可以自由组合，不能混成一件事：

```text
Wi-Fi STA：连接现有路由器
Wi-Fi SoftAP：自己创建无线热点
TCP Client：主动连接已知IP和端口
TCP Server：监听端口，等待客户端连接
```

本页现有描述对应：

```text
ESP8266 = Wi-Fi STA + TCP Client
PC      = 同一局域网中的 TCP Server
```

真正的“ESP创建热点并做服务器”应是：

```text
ESP8266 = Wi-Fi SoftAP + TCP Server
PC先连接ESP热点，再作为TCP Client连接ESP的IP和端口
```

SoftAP提供无线接入点，不必然等同于具备完整NAT、DHCP以外路由能力的家用路由器。

## 当前客户端模式的完整步骤

```text
AT
→ 设置STA模式
→ 加入2.4GHz Wi-Fi
→ 查询ESP分配到的局域网IP
→ PC网络助手先监听指定端口
→ ESP执行AT+CIPSTART连接PC的局域网IPv4和端口
→ 进入普通发送或单连接透传
```

PC地址必须是ESP8266能够路由到的地址。同一Wi-Fi下通常填写PC无线网卡的局域网IPv4；`127.0.0.1` 只代表PC自己，ESP8266不能用它找到PC。VPN、第三方防火墙、Windows防火墙和路由器的客户端隔离都可能阻止连接。

## 接收中断的职责

USART1回调只负责快速收字节、写缓冲区、推进解析状态并重新启动接收；连接成功、失败重试和GPIO业务放在主循环状态机中。中断回调里不要等待网络回复或执行长时间串口打印。
