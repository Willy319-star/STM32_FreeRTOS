# UART RX 中断与 CommandTask 阶段

## 阶段目标

本阶段给项目增加一个串口命令入口：

- PC 通过 USART1 发送字符命令；
- MCU 在 USART1 接收中断中读取字节；
- 中断把字节放入 FreeRTOS 队列；
- `CommandTask` 从队列中取出命令并执行；
- 串口返回状态信息。

这个阶段的重点不是命令本身多复杂，而是建立一条清晰的中断到任务的数据流。

## 支持的命令

当前命令是单字符命令：

```text
? 或 h : 打印帮助
s      : 打印系统状态，包括 tick、heap、min_heap
a      : 打印当前告警 EventGroup bits
```

例如发送 `s`，MCU 会返回：

```text
CMD STATUS tick=123456 heap=xxxxx min_heap=xxxxx
```

## 问题 → 操作 → 观察 → 结论

### 问题

串口接收数据以后，应该直接在中断里解析命令吗？

### 操作

本项目没有在 USART1 中断里直接解析命令，而是采用：

```text
USART1_IRQHandler
    -> 读取 1 字节
    -> xQueueSendFromISR()
    -> CommandTask
    -> 解析命令并打印结果
```

中断里只做最小工作：

1. 判断是否收到字节；
2. 读取 `DR` 寄存器；
3. 把字节发送到队列；
4. 如有需要，请求任务切换。

业务逻辑全部放到 `CommandTask` 中。

### 观察

串口启动后会看到：

```text
CommandTask start: USART1 RX IRQ -> queue -> task
CMD help:
  ? or h : show help
  s      : print system status
  a      : print alert bits
```

在串口助手或 PC 脚本中发送 `s`，会看到系统状态返回。

### 结论

这个设计体现了 RTOS 中断处理的基本原则：

- ISR 尽量短；
- ISR 不做复杂业务；
- ISR 不做阻塞操作；
- ISR 通过 `FromISR` API 把事件交给任务；
- 任务负责解析、打印和状态查询。

## 为什么用 Queue？

UART 接收是一个字节流，PC 什么时候发命令是不确定的。如果中断直接处理，很容易把业务逻辑塞进 ISR。

用 Queue 的好处是：

- ISR 和任务解耦；
- 可以缓冲突发输入；
- 命令任务可以阻塞等待队列，不消耗 CPU；
- 后续可以从单字符命令扩展成行命令或协议帧。

## 为什么不用普通 xQueueSend？

因为 USART1 中断发生在 ISR 上下文，不能使用普通任务上下文 API。

所以要使用：

```c
xQueueSendFromISR()
```

并在中断末尾调用：

```c
portYIELD_FROM_ISR(higher_priority_woken);
```

如果发送队列后唤醒了更高优先级任务，系统可以尽快切换到对应任务运行。

## 面试回答版本

问题：你项目里的串口接收是怎么做的？

30 秒回答：

我用 USART1 接收中断实现 PC 到 MCU 的命令入口。中断服务函数里只读取收到的 1 个字节，然后用 `xQueueSendFromISR()` 放入命令队列，不在中断里解析命令，也不打印串口。真正的命令解析放在 `CommandTask` 中完成，比如 `s` 打印系统状态，`a` 打印告警 EventGroup 状态。这样 ISR 很短，业务逻辑在任务中执行，符合 FreeRTOS 的中断设计习惯。

深入解释：

串口接收中断是异步事件，如果在 ISR 中做字符串解析、状态查询、串口打印，会导致中断执行时间变长，甚至引入阻塞风险。FreeRTOS 提供了 `FromISR` 系列 API，允许中断安全地通知任务。本项目通过队列传递收到的字节，CommandTask 阻塞等待队列，有命令才运行，没命令就让出 CPU。

项目结合：

这部分和前面的 UART 协议帧发送不同。协议帧主要解决 MCU 到 PC 的结构化数据上报；CommandTask 解决 PC 到 MCU 的控制入口。两者结合以后，项目既能主动上报传感器数据，也能被 PC 查询系统状态。

## 常见追问

### 问题：为什么中断里不能直接打印？

UART 打印可能等待 TXE 标志，或者等待互斥锁。中断里不能做可能阻塞的事情，否则会影响系统实时性。

### 问题：中断优先级为什么要注意？

如果 ISR 里调用 FreeRTOS 的 `FromISR` API，中断优先级必须满足 FreeRTOS 的限制，不能高于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`。否则可能破坏内核临界区。

### 问题：这个命令系统后续可以怎么扩展？

可以从单字符命令扩展为行协议，例如：

```text
GET STATUS
GET ALERT
SET PERIOD 500
```

也可以扩展为二进制协议帧，加入帧头、长度、命令字和 CRC。

## XCOM 与 PC 终端的使用区别

### 问题

为什么在 XCOM 中会看到乱码？

### 操作

当前项目有两类 UART 输出：

1. 文本日志：给人看的，例如 `DHT11 OK`、`CMD STATUS`、`HEARTBEAT`。
2. 二进制协议帧：给 PC 解析脚本看的，帧头是 `AA 55`，内容可能包含任意字节。

XCOM 默认按文本显示串口数据。如果二进制协议帧也在输出，XCOM 会把不可显示字节当成乱码、方块或奇怪汉字显示。

为了解决这个问题，项目现在默认关闭二进制协议帧。命令如下：

```text
b
```

发送一次 `b`：

```text
CMD BINARY ON: use PC parser, XCOM may show garbled bytes
```

再发送一次 `b`：

```text
CMD BINARY OFF: XCOM text mode
```

### 观察

在 XCOM 中调试命令时，应保持：

```text
CMD BINARY OFF
```

这样 XCOM 中主要看到可读文本。

当需要验证 `uart_protocol_parser.py` 时，再打开二进制帧：

```text
CMD BINARY ON
```

然后关闭 XCOM 串口连接，在 PC 终端运行：

```powershell
python scripts/uart_protocol_parser.py --port COM6 --baud 115200
```

### 结论

XCOM 适合观察文本日志和手动发送命令；PC 终端脚本适合解析二进制协议帧。两种工作流不要混在一起使用，否则 XCOM 看到二进制帧时就会显示乱码。
