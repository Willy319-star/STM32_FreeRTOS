# DHT11 queue stage

## 技术原理

本阶段把 DHT11 作为第一个业务外设接入 FreeRTOS 系统。DHT11 使用单总线
GPIO 时序通信，读取时需要微秒级延时，并通过校验和判断数据是否有效。

系统把 DHT11 采集任务和日志输出任务分开：

- `TaskDHT11`: 周期读取 DHT11，并把结果封装成 `AppMessage`。
- `TaskSensorLog`: 从 Queue 接收消息，并通过 UART 输出可观测日志。

## 为什么使用 Queue

Queue 用于任务之间传递数据。当前项目中，DHT11 任务是生产者，日志任务是
消费者。这样做的好处是：

1. 传感器采集逻辑和日志输出逻辑解耦。
2. 后续 MPU6050、VL53L0X、ADC 可以复用同一个消息通路。
3. 消息默认按值复制，不依赖 DHT11 任务中的局部变量生命周期。

## 高频面试题

### 问题：为什么 DHT11 不直接在采集任务里打印？

30 秒回答：

我把 DHT11 采集和日志输出拆成两个任务，DHT11 任务只负责按周期采样并通过
Queue 上报数据，日志任务统一消费消息并打印。这样传感器任务不会和显示、
串口等输出逻辑耦合，后续增加 MPU6050、VL53L0X 时也能复用同一套数据通路。

深入解释：

Queue 在 FreeRTOS 中默认复制数据，因此 `AppMessage` 从 DHT11 任务发送到
日志任务后，不依赖发送方栈上变量的生命周期。相比直接共享全局变量，这种
方式更清楚地表达了生产者和消费者关系，也更容易扩展错误统计和状态管理。

项目结合：

当前 DHT11 数据线使用 PC0。`TaskDHT11` 每 2 秒读取一次，读取成功时发送温度
和湿度，失败时发送错误原因。`TaskSensorLog` 收到消息后输出 `DHT11 OK` 或
`DHT11 FAIL`。

追问：

- Queue 传递的是数据还是指针？
- Queue 满了怎么办？
- DHT11 为什么需要微秒级延时？
- 为什么读取 DHT11 时有一段临界区？
- 如果 DHT11 一直 `idle-low`，你怎么定位？

### 问题：为什么这里用 Queue，而不是全局变量、信号量或 EventGroup？

30 秒回答：

我这里使用 Queue，是因为 DHT11 任务要传递的不是一个简单事件，而是一条
结构化数据消息。每次采样后会生成 `AppMessage`，里面包含消息类型、采集
tick、valid 标志、温度、湿度和错误信息。FreeRTOS Queue 默认按值复制消息，
接收任务不会依赖发送任务中局部变量的生命周期。相比全局变量，Queue 更适合
表达生产者-消费者关系；相比信号量、EventGroup、Task Notification，Queue
更适合携带结构化数据。

深入解释：

几种任务通信方式适合的场景不同：

- 全局变量适合保存“当前最新状态”，但要额外加 mutex，并且消费者不知道何时
  有新数据。
- Semaphore 适合通知“事件发生了”，但它本身不携带温湿度数据。
- Task Notification 很轻量，适合一对一通知或计数事件，但不适合传复杂结构体。
- EventGroup 适合表示状态位，例如 `DHT_OK`、`DISTANCE_ALERT`，不适合传温度、
  湿度、距离这类数据。
- Queue 适合一个或多个生产者向消费者发送固定格式消息，并保持消息顺序。

项目结合：

当前阶段只有 DHT11，但后续还会加入 MPU6050、VL53L0X 和 ADC 旋钮。使用
Queue 后，后续可以让多个传感器任务都发送同一种 `AppMessage`，再由状态任务
或日志任务统一消费。

追问：

- Queue 是复制数据还是传指针？
- 如果 Queue 满了，是阻塞、丢弃，还是覆盖旧数据？
- 多个传感器共用一个 Queue 时如何区分消息来源？
- 如果消息结构体很大，还适合直接通过 Queue 复制吗？

### 问题：Queue 需要自己设计消息格式吗？

30 秒回答：

需要。FreeRTOS Queue 只负责搬运固定大小的数据块，它不知道数据的业务含义。
所以项目里要自己设计消息结构体。当前使用 `AppMessage`，包含 `type`、`tick`、
`valid` 和具体传感器数据。创建队列时使用 `sizeof(AppMessage)`，发送和接收
时 Queue 会复制这个结构体。

深入解释：

当前消息格式核心字段如下：

```c
typedef enum {
    APP_MSG_DHT11 = 0,
} AppMessageType;

typedef struct {
    AppMessageType type;
    uint32_t tick;
    uint8_t valid;
    union {
        struct {
            uint8_t temperature;
            uint8_t humidity;
            const char *error;
        } dht11;
    } data;
} AppMessage;
```

`type` 用来区分消息来源。现在只有 DHT11，看起来多余，但后续可以扩展：

```c
typedef enum {
    APP_MSG_DHT11 = 0,
    APP_MSG_MPU6050,
    APP_MSG_VL53L0X,
    APP_MSG_ADC,
} AppMessageType;
```

`tick` 表示采样发生的时间，可用于判断采样周期和数据新鲜度。

`valid` 表示这次采样是否成功。成功时温湿度字段有效，失败时应查看错误信息。

当前 `error` 使用 `const char *` 是为了调试时可读性好，例如输出 `idle-low`、
`checksum`。如果后续做通信协议、日志存储或更严格的数据结构，可以改成错误码
枚举，避免传递字符串指针。

项目结合：

当前队列创建方式是：

```c
s_sensor_queue = xQueueCreate(8U, sizeof(AppMessage));
```

含义是队列最多缓存 8 条 `AppMessage`。发送时：

```c
xQueueSend(s_sensor_queue, &msg, 0U);
```

接收时：

```c
xQueueReceive(s_sensor_queue, &msg, portMAX_DELAY);
```

面试时可以强调：Queue 不是为了显得复杂，而是因为这个场景需要传递结构化
传感器数据，并且后续会有多个传感器生产消息。

### 问题：系统任务隔离是如何实现的？

30 秒回答：

我这里说的任务隔离主要是 FreeRTOS 调度层面的隔离，不是硬件 MPU 内存隔离。
项目中 Heartbeat、Worker、DHT11 和 SensorLog 被拆成独立任务，每个任务有
自己的栈和执行上下文。DHT11 任务只负责采集，通过 Queue 把 `AppMessage`
发给日志任务，不直接影响其他任务。同时 DHT11 驱动中所有等待传感器电平的
地方都有超时处理，所以传感器断开时不会死等，而是返回 `no-response-low`
错误。从日志可以看到 DHT11 FAIL 后 Heartbeat 和 Worker 仍然继续运行，
heap 保持稳定，这说明单个传感器故障被限制在局部，没有拖死整个 FreeRTOS
系统。

深入解释：

当前项目的任务隔离由几部分共同实现：

1. 功能拆成独立任务：
   `TaskHeartbeat` 负责系统心跳，`TaskWorker` 负责普通周期任务，
   `TaskDHT11` 负责传感器采集，`TaskSensorLog` 负责日志输出。

2. 每个任务有自己的栈：
   `xTaskCreate()` 创建任务时会为每个任务分配独立任务栈，所以 DHT11 任务
   中的局部变量不会和 Heartbeat 任务中的局部变量混在一起。

3. 任务之间通过 Queue 解耦：
   DHT11 任务不直接调用显示或复杂日志逻辑，只把消息发送到 Queue。日志任务
   从 Queue 中取数据并打印，这样采集逻辑和输出逻辑分离。

4. 外设等待有超时：
   DHT11 驱动中的 `wait_level()` 不会无限等待电平变化。如果传感器断开或
   没响应，会返回错误，任务可以继续下一轮调度。

项目结合：

故障测试日志：

```text
DHT11 OK tick=518001 temp=26C hum=43% reads=260 err=0
HEARTBEAT count=519 tick=519004 heap=32640
WORKER alive
DHT11 FAIL tick=520001 last=no-response-low reads=261 err=1
HEARTBEAT count=521 tick=521004 heap=32640
```

这段日志说明 DHT11 从正常变成异常后，系统没有卡死，Heartbeat 和 Worker
仍然继续运行，heap 也保持稳定。因此当前阶段验证了基本的故障隔离能力。

追问：

- FreeRTOS 的任务隔离和硬件 MPU 内存隔离有什么区别？
- 如果 DHT11 在临界区里无限等待，会发生什么？
- 为什么外设驱动里必须设计超时？
- Queue 解耦能解决哪些问题，不能解决哪些问题？
- 如何进一步证明任务栈没有溢出？

## 测试设计

正常测试：

- DHT11 DATA 接 PC0。
- 打开 USART1 115200 8N1。
- 观察每 2 秒出现 `DHT11 OK temp=... hum=...`。

边界测试：

- 连续运行 5 分钟，观察 `heap` 是否稳定。
- 观察 DHT11 `reads` 是否按 2 秒周期增长。

异常测试：

- 拔掉 DHT11 DATA，观察是否输出 `DHT11 FAIL last=...`。
- 将 DATA 短接 GND，观察是否出现 `idle-low`。
- 断开 DHT11 VCC，观察是否出现无响应类错误。

性能测试：

- 观察 heartbeat 是否仍然每 1 秒输出。
- 观察 DHT11 读取是否影响 Worker 任务 3 秒周期。
