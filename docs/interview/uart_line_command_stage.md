# UART 行命令解析阶段

## 阶段目标

上一阶段已经实现：

```text
USART1_IRQHandler -> xQueueSendFromISR() -> CommandTask
```

也就是串口接收中断只搬运字节，业务逻辑放到任务中处理。

本阶段继续升级 `CommandTask`，把单字符命令扩展为行命令解析器，让项目更接近真实嵌入式调试接口。

## 使用位置

本阶段命令在 **XCOM 串口助手** 中发送。

串口参数：

```text
COM口：你的 USB 串口，例如 COM6
波特率：115200
数据位：8
停止位：1
校验位：None
```

PC 终端暂时不用。PC 终端主要用于运行 `uart_protocol_parser.py` 解析二进制协议帧。

## 支持的命令

短命令和长命令都支持：

```text
? / H / HELP       显示帮助
S / STATUS         打印系统状态
A / ALERT          打印告警 EventGroup bits
B / BIN            切换二进制协议帧输出
BIN ON             打开二进制协议帧输出
BIN OFF            关闭二进制协议帧输出
```

建议在 XCOM 中常用：

```text
STATUS
ALERT
BIN OFF
```

如果只是快速测试，也可以发送：

```text
s
a
b
?
```

## 问题 → 操作 → 观察 → 结论

### 问题

UART 是字节流，不是消息流。PC 发送 `STATUS` 时，MCU 不是一次天然收到完整命令，而是一个字节一个字节触发接收中断。

那 MCU 怎么知道一条命令什么时候结束？

### 操作

`CommandTask` 内部维护一个小的命令缓冲区：

```text
收到 S -> 放入缓冲区
收到 T -> 放入缓冲区
收到 A -> 放入缓冲区
...
收到换行 或 短时间没有新字节 -> 认为一条命令结束
```

当前实现中：

- 命令最大长度是 32 字节；
- 收到 `\n` 会立即执行命令；
- 如果已经收到部分命令，但 50ms 没有新字节，也会自动执行；
- 命令会自动去掉前后空格，并转成大写。

所以 XCOM 里发送：

```text
status
```

也能被识别成：

```text
STATUS
```

### 观察

发送：

```text
STATUS
```

应看到：

```text
CMD STATUS tick=... heap=... min_heap=... binary=OFF
```

发送：

```text
ALERT
```

应看到：

```text
CMD ALERT bits=0x....
```

发送：

```text
BIN ON
```

应看到：

```text
CMD BINARY ON: use PC parser, XCOM may show garbled bytes
```

发送：

```text
BIN OFF
```

应看到：

```text
CMD BINARY OFF: XCOM text mode
```

### 结论

这个阶段解决的是 UART 命令边界问题：

- ISR 仍然只负责收字节；
- Queue 负责把字节从 ISR 安全传给任务；
- CommandTask 负责组包、识别命令边界、解析命令；
- 业务处理不进入中断。

这比单字符命令更接近真实工程里的串口调试协议。

## 为什么需要 50ms 短超时？

不同串口助手的“单条发送”行为不完全一样：

- 有的会自动带 `\r\n`；
- 有的只发送你输入的字符；
- 有的连续发送多个字节，中间间隔很短。

如果只依赖换行符，用户发送 `s` 但没有换行时，MCU 可能一直等不到命令结束。

所以本项目增加了 50ms 空闲超时：

- 如果收到 `s` 后 50ms 没有新字节，就执行 `s`；
- 如果收到 `BIN ON`，这些字节会连续到达，等整串结束后再执行。

这让 XCOM 操作更方便。

## 为什么不用 sscanf 做复杂解析？

当前命令很少，直接用 `strcmp()` 更简单、可控、占用小。

后续如果命令变成：

```text
SET PERIOD DHT 2000
SET THRESHOLD NEAR 120
```

再考虑引入参数解析函数。

## 面试回答版本

问题：你是怎么处理 UART 命令接收的？

30 秒回答：

我没有在串口中断里直接解析命令，而是在 USART1 RX 中断里只读取 1 个字节，然后用 `xQueueSendFromISR()` 送给 `CommandTask`。`CommandTask` 内部维护命令缓冲区，把连续收到的字节组装成一条命令，遇到换行或者 50ms 没有新字节就认为命令结束，然后再解析 `STATUS`、`ALERT`、`BIN ON/OFF` 等命令。这样 ISR 很短，命令解析在任务上下文执行，既安全又容易扩展。

深入解释：

UART 本质是字节流，没有天然消息边界。所以接收端必须自己定义命令结束条件。这个项目同时支持换行结束和短超时结束，兼容不同串口助手的发送方式。命令解析前还会去掉空格并转成大写，这样 `status`、`STATUS` 都能识别。

项目结合：

这个命令系统和之前的二进制协议帧是两个方向：

- `CommTxTask`：MCU 主动向 PC 上报传感器数据；
- `CommandTask`：PC 主动查询或控制 MCU。

两者组合后，项目既能上报实时数据，也能通过串口命令观察系统状态和切换输出模式。

