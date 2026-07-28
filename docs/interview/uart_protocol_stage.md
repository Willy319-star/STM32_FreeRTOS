# UART protocol stage

## 阶段目标

当前项目已经能通过 UART 打印传感器日志，但日志是给人看的文本，不适合 RK3568 或 PC 程序稳定解析。

本阶段开始设计 MCU 到上位机的二进制协议帧，先完成协议打包模块：

- `app_protocol.h`
- `app_protocol.c`

暂时只做发送帧构造，不做 UART 接收、命令解析、DMA 和 IDLE 中断。

## 为什么不能只用 printf 日志

问题：

UART 是字节流，没有天然消息边界。文本日志虽然方便观察，但程序端很难可靠判断一条传感器数据从哪里开始、到哪里结束。

操作：

设计固定帧头、长度字段和 CRC。

观察：

上位机可以从任意字节流中搜索帧头，根据长度等待完整帧，再用 CRC 判断数据是否正确。

结论：

协议帧可以支持半包、连续帧、错位恢复和错误帧丢弃，是后续 Linux 网关解析的基础。

## 当前帧格式

| 字段 | 长度 | 说明 |
| --- | --- | --- |
| SOF0 | 1 字节 | 帧头 0xAA |
| SOF1 | 1 字节 | 帧头 0x55 |
| version | 1 字节 | 协议版本，目前 0x01 |
| msg_type | 1 字节 | 消息类型 |
| seq | 2 字节 | 序列号，小端 |
| payload_len | 2 字节 | 数据区长度，小端 |
| timestamp | 4 字节 | FreeRTOS tick，小端 |
| payload | N 字节 | 传感器数据 |
| crc16 | 2 字节 | CRC16，小端 |

头部长度为 12 字节：

```text
AA 55 version type seq_l seq_h len_l len_h tick0 tick1 tick2 tick3
```

CRC 覆盖范围：

```text
SOF0 到 payload 最后一个字节
```

CRC 不覆盖 CRC 自己。

## 消息类型

```c
typedef enum {
    APP_PROTO_MSG_DHT11     = 0x01,
    APP_PROTO_MSG_MPU6050   = 0x02,
    APP_PROTO_MSG_VL53L0X   = 0x03,
    APP_PROTO_MSG_POT       = 0x04,
    APP_PROTO_MSG_HEARTBEAT = 0x10,
} AppProtoMsgType;
```

## 为什么使用小端

STM32 是小端 CPU，Linux/RK3568 通常也是小端。协议字段使用小端可以让 MCU 打包和上位机解析都更直接。

但面试时要强调：协议一旦定义，就必须在文档中固定字节序，不能依赖不同平台的结构体内存布局。

## 为什么不用直接发送结构体

直接发送 C 结构体有几个问题：

- 编译器可能插入 padding；
- 不同平台字节序可能不同；
- `const char *` 这类指针字段不能跨设备传输；
- 结构体升级时兼容性差；
- 上位机难以处理半包和错位。

所以当前协议采用手动写入字节数组的方式：

```c
put_u16_le(...)
put_u32_le(...)
```

这样每个字节的位置都是确定的。

## 当前接口

```c
uint16_t AppProtocol_Crc16(const uint8_t *data, uint16_t len);

uint16_t AppProtocol_BuildFrame(uint8_t msg_type,
                                uint16_t seq,
                                uint32_t timestamp,
                                const uint8_t *payload,
                                uint16_t payload_len,
                                uint8_t *out,
                                uint16_t out_size);
```

`AppProtocol_BuildFrame()` 返回最终帧长度。

返回 `0` 表示打包失败，常见原因：

- 输出缓冲区为空；
- payload 指针为空但长度不为 0；
- payload 超过最大长度；
- 输出缓冲区不够。

## 为什么第一阶段只做打包

协议开发不应该一开始就同时做发送、接收、DMA、IDLE 中断、环形缓冲区和命令解析。

当前先完成最小闭环：

```text
payload -> BuildFrame -> UART 发送 -> PC 解析
```

等发送帧稳定后，再加入：

- `CommTxTask`
- UART DMA 发送
- UART IDLE 接收
- RingBuffer
- CommandTask
- 协议状态机

## 下一阶段计划

下一步新增 `CommTxTask`：

```text
SensorTask
   -> s_sensor_queue
   -> TaskSensorLog / DataProcess
   -> s_comm_queue
   -> CommTxTask
   -> AppProtocol_BuildFrame()
   -> UART
```

这样 UART 只有一个发送出口，避免多个任务直接抢串口。

## 面试回答

问题：为什么你的项目要设计 UART 协议，而不是直接 printf？

30 秒回答：

我一开始用 printf 做调试日志，方便观察传感器是否工作。但后续要接 RK3568 Linux 网关，文本日志不适合程序稳定解析。所以我设计了二进制帧格式，包括帧头、版本、消息类型、序列号、长度、时间戳、payload 和 CRC16。帧头和长度用于处理半包、连续帧和错位恢复，CRC 用于检测传输错误。协议字段手动按小端写入字节数组，避免直接发送结构体带来的 padding、字节序和指针问题。

追问：CRC 校验覆盖哪些内容？

回答：

CRC 覆盖从帧头到 payload 的所有字节，不覆盖 CRC 字段本身。接收端收到完整帧后，用同样算法重新计算 CRC，与帧尾 CRC 比较，如果不一致就丢弃该帧并更新错误计数。

追问：为什么要有 length 字段？

回答：

UART 是字节流，可能一次收到半帧，也可能一次收到多帧。length 字段可以告诉接收端 payload 有多长，接收端只有等到固定头部、payload 和 CRC 都收齐后才进行校验和分发。
## CommTxTask 阶段

问题：
已经有 `DHT11/MPU6050/VL53L0X/POT -> s_sensor_queue -> TaskSensorLog`，为什么还要新增 `CommTxTask`？

操作：
新增 `app_comm.c/.h`，在内部创建 `s_comm_queue` 和 `CommTxTask`。`TaskSensorLog` 每收到一条 `AppMessage` 后，先更新本地状态快照，再调用 `AppComm_SubmitSensorMessage()` 把数据复制成通信消息。`CommTxTask` 从通信队列取消息，调用 `AppProtocol_BuildFrame()` 打包，再通过 UART 发送二进制帧。

观察：
UART 现在同时有两类输出：一类是给人看的文本日志，例如 `DHT11 OK`、`MPU6050 OK`；另一类是给程序解析的二进制帧，帧头是 `AA 55`。在普通串口助手里，二进制帧可能显示成乱码，这是正常现象。后续 PC/RK3568 解析程序应该从字节流中搜索 `AA 55`，再按 length 和 CRC 判断完整帧。

结论：
`CommTxTask` 的意义是把“业务采集”和“通信发送”隔离开。传感器任务只负责采集，汇总任务只负责状态和日志，通信任务只负责协议帧发送。这样后续把 UART 发送改成 DMA、增加重传、增加 Linux 网关解析时，不需要改每个传感器任务。

## 当前 payload 格式

所有 payload 都只发送固定长度的数值字段，不发送 `const char *` 字符串指针。原因是指针只是 MCU 内存地址，上位机无法解释。

| msg_type | payload 字段 |
| --- | --- |
| `0x01 DHT11` | `valid(1), temperature(1), humidity(1)` |
| `0x02 MPU6050` | `valid(1), addr(1), whoami(1), ax_i16, ay_i16, az_i16, gx_i16, gy_i16, gz_i16` |
| `0x03 VL53L0X` | `valid(1), addr(1), model_id(1), range_valid(1), distance_mm_u16` |
| `0x04 POT` | `valid(1), raw_u16, percent(1)` |

## 为什么 comm_queue 用非阻塞发送

问题：
`AppComm_SubmitSensorMessage()` 为什么使用 `xQueueSend(..., 0)`，而不是一直等待？

操作：
通信队列作为调试/上报链路，满了就打印 `COMM queue full`，丢弃本次上报。

观察：
如果通信发送被串口速度限制拖慢，非阻塞发送不会反过来卡住 `TaskSensorLog`，也不会进一步影响传感器采集任务。

结论：
这个阶段优先保证采集链路实时性。通信丢一帧可以通过下一次采样恢复，但如果采集任务被通信阻塞，系统实时性会变差。面试时可以说：这是典型的“弱实时上报”和“采集实时性”之间的取舍。

## 面试回答：为什么不用每个传感器任务直接发 UART

30 秒回答：
我没有让每个传感器任务直接操作 UART，而是统一走 `CommTxTask`。原因是 UART 是共享外设，如果多个任务同时发送，容易出现输出交叉、协议帧被插入日志、后续 DMA 也不好管理。所以我让传感器任务只发 `AppMessage`，汇总任务复制成 `CommMessage`，最后由 `CommTxTask` 统一打包和发送。这样通信出口唯一，任务边界清晰，后续增加 CRC、序号、重传、DMA 或 Linux 网关解析都更容易维护。

## PC 端协议解析脚本阶段

问题：
XCOM 里已经能看到文本日志和一些乱码，为什么还要写 PC 端解析脚本？

操作：
新增 `scripts/uart_protocol_parser.py`。脚本从串口、二进制文件或十六进制字符串读取字节流，维护一个接收缓冲区，搜索帧头 `AA 55`，再读取 `version/type/seq/length/tick/payload/crc16`。收到完整帧后先校验 CRC，再根据 `msg_type` 解析 payload。

观察：
普通串口助手把二进制帧当文本显示，所以会出现方块、问号和乱码。解析脚本不按文本行处理，而是按字节流处理，因此可以从“文本日志 + 二进制帧”混合输出中找到真正的协议帧。

结论：
这一步证明了协议从“MCU 能发送”进入到“上位机能解析”的阶段。后续 RK3568/Linux 网关程序可以复用同样的解析思路：串口读字节、找帧头、按长度收完整帧、CRC 校验、按消息类型分发。

## 解析脚本使用方式

实时读取串口：

```powershell
python scripts/uart_protocol_parser.py --port COM6 --baud 115200
```

如果还想同时显示夹杂的 ASCII 文本日志：

```powershell
python scripts/uart_protocol_parser.py --port COM6 --baud 115200 --show-text
```

注意：串口同一时间只能被一个程序占用。运行脚本前要先关闭 XCOM 的串口连接。

离线解析十六进制字符串：

```powershell
python scripts/uart_protocol_parser.py --hex "AA 55 01 01 07 00 03 00 E8 03 00 00 01 1A 2A 34 DF"
```

自测观察：

```text
[DHT11 OK] seq=7 tick=1000 temp=26C hum=42%
```

## 面试回答：UART 接收端为什么要做状态机/缓冲区

30 秒回答：
UART 是字节流，不保证一次 `read` 就刚好读到一帧。可能出现半包、粘包、前面夹杂调试日志、或者从中间字节开始读取。所以接收端不能简单按行解析，而应该维护一个缓冲区，从里面搜索固定帧头 `AA 55`。找到帧头后读取长度字段，等完整帧到齐后再校验 CRC。如果 CRC 不对，就丢弃当前候选帧并继续向后找帧头。这样系统可以从错位和噪声中恢复。

## 协议帧到底解决了什么

问题：
之前直接在 XCOM 里看 `DHT11 OK`、`MPU6050 OK` 不是也能看到数据吗？为什么还要设计协议帧和 PC 解析脚本？

操作：
把 UART 输出分成两种用途：

1. 文本日志：给人调试看，例如 `POT OK tick=... raw=...`。
2. 协议帧：给程序稳定解析，例如 `AA 55 + header + payload + CRC`。

观察：
XCOM 适合人工观察，但它看到的是一串字符。人可以凭眼睛判断哪一行是温湿度、哪一行是距离，但程序不能可靠地“猜”。如果以后 RK3568/Linux 网关要自动接收数据、存数据库、上传云端、画曲线，就必须知道每条数据从哪里开始、到哪里结束、是什么类型、是否损坏。

结论：
协议帧主要解决四类问题：

1. 消息边界：UART 是字节流，本身没有“这一条消息结束了”的概念。帧头 `AA 55` 和 `length` 用来确定一帧的开始和长度。
2. 数据类型：`msg_type` 告诉接收端这是 DHT11、MPU6050、VL53L0X 还是电位器数据。
3. 数据完整性：`CRC16` 判断传输过程中有没有丢字节、错字节、错位。
4. 自动化解析：上位机程序不需要读中文/英文日志，而是按固定格式解析字段。

## 为什么 XCOM 不能作为正式通信方案

问题：
XCOM 里能看到数据，为什么还说它不适合正式通信？

操作：
对比“人看日志”和“程序解析协议帧”。

观察：
文本日志存在几个问题：

- 字段不固定：今天打印 `temp=26C`，明天可能改成 `temperature=26`，上位机解析就会坏。
- 没有强边界：串口接收可能一次收到半行，也可能一次收到多行。
- 难判断错误：如果中间丢了一个字符，人可能还能猜，程序很难知道这条数据是否可信。
- 流量更大：文本包含很多无关字符，例如单词、空格、单位。
- 不适合二进制数据：IMU、距离、ADC 这些数值本来就是二进制，转成字符串再解析效率低。

结论：
XCOM 是调试工具，不是通信协议。项目开发初期用 XCOM 很好，因为它能快速证明外设活着；但项目要升级成“MCU + Linux 网关/上位机”的系统时，就必须使用协议帧。

## 协议帧每个字段的作用

当前帧格式：

```text
AA 55 version type seq length tick payload crc16
```

字段解释：

| 字段 | 作用 |
| --- | --- |
| `AA 55` | 帧头，用来从连续字节流中找到一帧的开始 |
| `version` | 协议版本，后续升级协议时兼容旧版本 |
| `type` | 消息类型，区分 DHT11、MPU6050、VL53L0X、POT |
| `seq` | 序号，用来观察是否丢帧、乱序、重复 |
| `length` | payload 长度，用来处理半包和粘包 |
| `tick` | MCU 侧时间戳，用来分析任务周期和数据时间 |
| `payload` | 真正的传感器数据 |
| `crc16` | 校验帧是否损坏 |

## PC 解析脚本的技术点

问题：
`uart_protocol_parser.py` 的核心技术点是什么？

操作：
脚本没有按文本行读取，而是按字节流读取。每次串口读到一批 bytes 后，放入 `bytearray` 缓冲区，然后重复执行：

```text
搜索 AA55
  -> 不够 header 就继续等
  -> 读 length
  -> 不够完整帧就继续等
  -> 校验 CRC
  -> 按 type 解析 payload
  -> 输出结构化结果
```

观察：
这套逻辑可以处理：

- 半包：一次只收到一部分帧，脚本会等待后续字节。
- 粘包：一次收到多帧，脚本会循环解析。
- 夹杂文本日志：脚本会跳过非 `AA 55` 的内容。
- 错误帧：CRC 不对就打印 `CRC_OR_FORMAT_ERROR` 或丢弃。

结论：
这个脚本模拟了以后 Linux 网关里串口接收模块的核心逻辑。面试时可以说：我不是只会 `printf`，而是完成了从 MCU 采集、协议封装、UART 发送、PC 端状态机解析、CRC 校验的完整链路。

## 面试回答：为什么要做协议帧

30 秒回答：
一开始我用 XCOM 看文本日志，是为了快速验证外设能不能工作。但文本日志只适合人看，不适合程序稳定解析。UART 本身是字节流，没有消息边界，所以我设计了协议帧，包括帧头、版本、类型、序号、长度、时间戳、payload 和 CRC。帧头和长度解决半包、粘包和错位恢复；类型用于区分不同传感器；序号用于观察丢帧；CRC 用于判断数据是否损坏。这样后续 RK3568 或 PC 上位机就可以稳定解析 MCU 数据，而不是依赖字符串匹配。

## 面试追问：如果只用 XCOM 会有什么风险

回答：
只用 XCOM 的风险是系统停留在“人工调试”阶段。人能看懂日志，但程序不能可靠判断每条消息的边界、类型和正确性。如果日志格式变了、串口半包了、数据中间丢了字符，基于字符串的解析很容易出错。协议帧把数据格式固定下来，并通过 length 和 CRC 提供恢复和校验能力，更适合工程化通信。

## 通信健壮性测试阶段

问题：
真实串口通信不一定每次都刚好收到一帧完整数据。如何证明 PC 端解析脚本不是只能解析“理想情况”？

操作：
新增 `scripts/test_uart_protocol_parser.py`，用 Python `unittest` 构造协议帧并喂给 `StreamParser`。测试内容包括：

1. 单个 DHT11 正常帧。
2. 半包：一帧拆成两次输入，第一次不输出，第二次补齐后输出。
3. 粘包：一次输入两帧，解析器按顺序输出两条数据。
4. 文本噪声：在协议帧前夹杂 `HEARTBEAT` 文本日志。
5. CRC 错误：先输入一帧损坏数据，再输入一帧正常数据，解析器能报错并恢复。
6. MPU6050 有符号数解析：验证负数加速度/角速度能正确按小端 `int16` 还原。

观察：
运行命令：

```powershell
python -m unittest scripts.test_uart_protocol_parser
```

测试结果：

```text
Ran 6 tests
OK
```

结论：
协议解析已经从“能解析真实串口数据”进一步提升到“对异常输入有验证”。这能证明解析器具备处理 UART 字节流常见问题的能力：半包、粘包、噪声、错误帧和有符号数解析。

## 为什么 CRC 错误后只滑动 1 字节

问题：
解析器发现 CRC 错误后，为什么不是直接丢掉整帧长度，而是丢掉 1 个字节继续搜索？

操作：
在 `StreamParser.feed()` 中，如果候选帧 CRC 或格式错误，就输出 `CRC_OR_FORMAT_ERROR`，然后 `del self.buf[0]`，只删除当前候选帧的第一个字节。

观察：
如果直接丢掉整段候选帧，而这段数据中间恰好包含下一个合法帧头 `AA 55`，就可能把后面的好帧一起丢掉。滑动 1 字节虽然多做几次搜索，但恢复能力更强。

结论：
这是典型的串口协议重同步策略。遇到错误时不要假设当前 length 一定可信，而是保守地滑动窗口重新找帧头，提高从错位和噪声中恢复的概率。

## 面试回答：你怎么验证协议解析的健壮性

30 秒回答：
我除了用真实串口验证正常数据，还写了 PC 端单元测试，专门构造半包、粘包、文本噪声、CRC 错误和有符号数 payload。测试里不是按行读，而是把 bytes 分批喂给状态机，观察它是否能等待完整帧、连续解析多帧、跳过文本日志、发现 CRC 错误并重新同步。这样能证明我的协议解析不是只在理想情况下可用，而是考虑了 UART 字节流的实际问题。
