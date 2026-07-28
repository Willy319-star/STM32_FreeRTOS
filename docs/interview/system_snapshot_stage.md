# 统一系统状态快照阶段

## 问题

项目里的数据来源越来越多：

```text
DHT11      温湿度
MPU6050    加速度、角速度、运动状态
VL53L0X    距离
ADC        电位器百分比
EventGroup 告警状态
Heap       剩余内存
```

如果 LCD、UART STATUS、协议上报、告警逻辑都各自去拼这些数据，系统会变得分散：

```text
数据来源不统一
多个模块重复保存状态
不同模块读到的数据不一致
后续扩展远程上报或日志诊断时很乱
```

所以本阶段增加一份统一系统状态快照。

## 操作

新增模块：

```text
app_snapshot.h
app_snapshot.c
```

核心结构体：

```c
typedef struct {
    uint32_t tick;
    uint32_t heap_free;
    uint32_t heap_min;
    uint32_t alert_bits;

    uint8_t dht_valid;
    uint8_t dht_stale;
    uint8_t temperature;
    uint8_t humidity;

    uint8_t mpu_valid;
    int16_t ax_mg;
    int16_t ay_mg;
    int16_t az_mg;
    int16_t gx_dps;
    int16_t gy_dps;
    int16_t gz_dps;
    uint8_t motion;
    uint8_t posture_changed;

    uint8_t vl53_valid;
    uint8_t vl53_range_valid;
    uint16_t distance_mm;

    uint8_t pot_valid;
    uint8_t pot_percent;
    uint16_t pot_raw;
} AppSystemSnapshot;
```

核心 API：

```c
void AppSnapshot_Init(void);
void AppSnapshot_UpdateFromSensor(const AppMessage *msg);
uint8_t AppSnapshot_Get(AppSystemSnapshot *snapshot);
```

## 数据流

传感器任务不直接改屏幕，也不直接改 STATUS：

```text
DHT11 Task
MPU6050 Task
VL53L0X Task
POT Task
    -> xQueueSend(AppMessage)
        -> TaskSensorLog
            -> AppSnapshot_UpdateFromSensor()
```

其他模块读取统一快照：

```text
LCD Task
    -> AppSnapshot_Get()
    -> 刷新 ST7735S

Command Task
    -> STATUS
    -> AppSnapshot_Get()
    -> 打印 CMD SNAP ...
```

## 如何保证线程安全

快照内部使用 mutex：

```c
static SemaphoreHandle_t s_snapshot_mutex;
```

更新快照时：

```c
xSemaphoreTake(s_snapshot_mutex, ...)
更新 s_snapshot
xSemaphoreGive(s_snapshot_mutex)
```

读取快照时：

```c
xSemaphoreTake(s_snapshot_mutex, ...)
复制一份 s_snapshot 到调用者传入的结构体
xSemaphoreGive(s_snapshot_mutex)
```

注意：读取时不是返回全局指针，而是复制一份快照。

这样做的好处是：

```text
调用者拿到的是稳定副本
不会长期占用 mutex
LCD 刷屏时不会阻塞传感器状态更新
```

## 为什么快照里还包含 heap 和 alert_bits

`tick`、`heap_free`、`heap_min`、`alert_bits` 不是由传感器 Queue 直接产生的。

所以 `AppSnapshot_Get()` 在返回前动态补充：

```text
tick       = xTaskGetTickCount()
heap_free  = xPortGetFreeHeapSize()
heap_min   = xPortGetMinimumEverFreeHeapSize()
alert_bits = AppControl_GetAlertBits()
```

这样 STATUS 命令拿到的是一份更完整的系统状态。

## 观察

XCOM 发送：

```text
STATUS
```

会看到类似：

```text
CMD STATUS tick=... heap=... min_heap=... binary=OFF
CMD STATUS log=INFO
CMD SNAP DHT=1 temp=25C hum=58% stale=0
CMD SNAP VL53=1 range=1 dist=88mm POT=27% raw=1106
CMD SNAP MPU=1 acc=-950,1,-404 motion=0 posture=0 alert=0x0000
```

这几行说明 STATUS 已经不是临时从各个任务拼数据，而是读取统一快照。

## 结论

统一快照的意义是：

```text
传感器任务负责采集
汇总任务负责更新快照
显示/命令/协议模块负责读取快照
```

这样模块之间不需要互相知道对方内部状态，系统结构更清楚，也更适合面试解释。

## 模块耦合是什么意思

模块耦合指的是一个模块对另一个模块的依赖程度。

在这个项目里，如果 LCD 想显示温湿度、距离和电位器数据，有两种做法。

高耦合做法：

```text
LCD Task 直接读取 DHT11 Task 的变量
LCD Task 直接读取 MPU6050 Task 的变量
LCD Task 直接读取 VL53L0X Task 的变量
LCD Task 直接读取 POT Task 的变量
```

这样 LCD 就需要知道很多其他模块的内部细节：

```text
DHT11 的变量叫什么
MPU6050 的数据放在哪里
VL53L0X 的状态怎么表示
POT 的滤波值保存在哪里
```

这就是高耦合。问题是，只要某个传感器模块的数据结构改了，LCD、STATUS、协议上报这些模块可能都要跟着改。

低耦合做法：

```text
DHT11 / MPU6050 / VL53L0X / POT
    -> Queue
        -> TaskSensorLog
            -> AppSnapshot_UpdateFromSensor()
                -> AppSystemSnapshot

LCD / STATUS / 协议上报
    -> AppSnapshot_Get()
```

这时 LCD 不关心 DHT11 是怎么读的，也不关心 VL53L0X 是怎么初始化的，只关心统一快照里当前的温度、湿度、距离、电位器百分比。

## 为什么要避免高耦合

高耦合会带来几个问题：

```text
1. 一个模块改动，很多模块跟着改
2. 调试时很难判断问题属于谁
3. 代码复用性差
4. 后期新增功能容易破坏旧功能
5. 系统架构不好解释
```

统一系统状态快照就是为了降低耦合：

```text
传感器任务负责采集
TaskSensorLog 负责汇总消息
AppSnapshot 负责保存统一状态
LCD / STATUS / 协议模块只读取快照
```

这样后续如果新增蓝牙上报、数据存储、远程监控，只需要读取 `AppSnapshot_Get()`，不用再分别去依赖 DHT11、MPU6050、VL53L0X、POT 的内部实现。

面试可以这样说：

> 模块耦合指的是模块之间相互依赖的程度。耦合越高，一个模块的修改越容易影响其他模块，系统维护和调试都会变困难。我在项目中通过 Queue 和统一系统状态快照降低耦合。传感器任务只负责采集并上报消息，LCD、UART STATUS、协议上报不直接访问传感器内部变量，而是读取统一快照。这样模块职责更清楚，后续扩展和调试也更方便。

## 面试回答

问题：你的系统状态是怎么统一管理的？

30 秒回答：

我在项目中设计了统一系统状态快照 `AppSystemSnapshot`。各个传感器任务通过 FreeRTOS Queue 上报 `AppMessage`，汇总任务收到消息后调用 `AppSnapshot_UpdateFromSensor()` 更新全局快照，快照内部用 mutex 保护。LCD 刷新和串口 `STATUS` 命令不直接访问各个传感器任务，而是通过 `AppSnapshot_Get()` 获取一份稳定副本。这样可以降低模块耦合，保证显示、命令和后续协议上报使用同一份状态来源。

追问：为什么读取快照时要复制一份，而不是直接返回指针？

回答：

如果直接返回全局指针，调用者可能在使用过程中遇到其他任务同时更新，数据一致性不好；而且调用者可能长时间持有这个指针，影响同步设计。复制一份快照的成本很低，但能保证调用者拿到的是一个稳定副本，读取后就可以释放 mutex，适合 LCD 刷新、STATUS 打印这类场景。

## 测试方法

正常测试：

```text
XCOM 发送 STATUS
观察 CMD SNAP 中 DHT、VL53、POT、MPU 是否都有数据
```

一致性测试：

```text
旋转电位器
观察 LCD 页面变化
同时发送 STATUS
确认 POT 百分比和屏幕页面选择一致
```

异常测试：

```text
拔掉某个传感器
观察对应 valid 变为 0
其他传感器快照仍然继续更新
```

并发测试：

```text
长时间运行
观察 heap/min_heap 稳定
确认没有因为快照 mutex 导致任务卡死
```
