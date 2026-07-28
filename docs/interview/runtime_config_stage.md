# 运行时告警阈值配置阶段

## 这一阶段做了什么

本阶段把告警阈值从固定宏改成运行时配置，并通过 XCOM 串口命令查询和修改。

支持命令：

```text
CONFIG
GET CONFIG
SET HOT <温度C>
SET DRY <湿度百分比>
SET NEAR <距离mm>
SET POT <电位器百分比>
SET MOTION <角速度dps>
```

示例：

```text
CONFIG
SET NEAR 80
SET POT 90
SET HOT 35
```

注意：这些命令是在 XCOM 里发送，不是在电脑 CMD 终端里执行。

## 功能是怎么实现的

### 问题

`CONFIG` 和 `SET NEAR 80` 这些命令，是怎么从 XCOM 发送后影响 MCU 内部告警逻辑的？

### 操作

完整数据流如下：

```text
XCOM 输入 CONFIG / SET NEAR 80
        ↓
USART1 接收到字节
        ↓
USART1_IRQHandler 中断触发
        ↓
AppCommand_OnRxByteFromISR()
        ↓
字节放入 FreeRTOS Queue
        ↓
CommandTask 从 Queue 取字节，拼成一行命令
        ↓
execute_command() 解析命令
        ↓
调用 AppControl_GetConfig() 或 AppControl_SetDistanceNear()
        ↓
修改 app_control.c 里的 s_config
        ↓
后续传感器数据进入 AppControl_UpdateFromSensor()
        ↓
使用新的阈值判断 ALERT bits
```

对应代码位置：

```text
core/src/interrupts_freertos.c
    USART1_IRQHandler()

app/STM32F407_FreeRTOS_multisensors/app_command.c
    AppCommand_OnRxByteFromISR()
    TaskCommand()
    execute_command()
    print_config()
    execute_set_command()

app/STM32F407_FreeRTOS_multisensors/app_control.c
    s_config
    AppControl_GetConfig()
    AppControl_SetDistanceNear()
    AppControl_UpdateFromSensor()
```

### 观察

在 XCOM 发送：

```text
CONFIG
```

命令任务会调用 `AppControl_GetConfig()`，然后打印：

```text
CMD CONFIG hot=32C dry=30% near=120mm pot=80% motion=100dps
```

在 XCOM 发送：

```text
SET NEAR 80
```

命令任务会解析出：

```text
命令名：SET
配置项：NEAR
参数值：80
```

然后调用：

```c
AppControl_SetDistanceNear(80);
```

成功后打印：

```text
CMD SET NEAR OK value=80
CMD CONFIG hot=32C dry=30% near=80mm pot=80% motion=100dps
```

### 结论

这部分不是简单地“串口打印”，而是形成了一条 PC 到 MCU 的控制链路：

```text
PC 命令输入 -> UART 中断接收 -> Queue 解耦 -> 命令任务解析 -> 控制模块改配置 -> 告警逻辑使用新参数
```

所以它实现的是运行时控制能力，而不是单纯的日志输出能力。

## 为什么中断里只投递字节

### 问题

为什么不直接在 `USART1_IRQHandler()` 里面解析 `CONFIG` 或 `SET NEAR 80`？

### 操作

当前设计是：

```text
中断里：
    只读取 1 个 UART 字节
    只放入 FreeRTOS Queue

任务里：
    拼接字符串
    判断换行或超时
    解析 CONFIG / SET
    调用控制模块 API
```

### 观察

字符串解析、`strcmp()`、`snprintf()`、配置修改、串口打印这些操作耗时都比“收 1 个字节”长。

如果把它们放在中断里，会导致：

```text
中断执行时间变长
其他中断响应变慢
FreeRTOS 调度受影响
串口打印和传感器采集更容易互相影响
```

### 结论

中断只做最短路径的数据搬运，复杂逻辑放到任务里处理。

这也是嵌入式面试中经常强调的原则：

```text
ISR should be short and deterministic.
```

也就是：中断服务函数要短、快、可预测。

## 问题

为什么不能继续把阈值写死在代码里？

## 操作

原来告警判断直接使用宏：

```c
ALERT_TEMP_HOT_C
ALERT_DISTANCE_NEAR_MM
ALERT_POT_HIGH_PCT
```

现在改成 `AppControlConfig`：

```c
typedef struct {
    uint8_t temp_hot_c;
    uint8_t humidity_dry_pct;
    uint16_t distance_near_mm;
    uint8_t pot_high_pct;
    int16_t gyro_motion_dps;
} AppControlConfig;
```

`AppControl_UpdateFromSensor()` 每次判断告警前先复制一份配置快照，然后用快照里的阈值判断。

## 观察

在 XCOM 发送：

```text
CONFIG
```

应该看到：

```text
CMD CONFIG hot=32C dry=30% near=120mm pot=80% motion=100dps
```

发送：

```text
SET NEAR 80
```

应该看到：

```text
CMD SET NEAR OK value=80
CMD CONFIG hot=32C dry=30% near=80mm pot=80% motion=100dps
```

如果 VL53L0X 距离小于 80mm，下一次传感器数据更新后，`ALERT near=1`。

## 结论

运行时配置解决的是“参数调试效率”和“系统可维护性”问题。

如果阈值写死在代码里，每改一次近距离告警阈值都要：

```text
改代码 -> 编译 -> 烧录 -> 观察
```

现在可以直接：

```text
XCOM 发命令 -> 观察 ALERT 输出
```

这更接近真实项目里的参数配置流程。

## 为什么用串口命令配置

### 问题

运行时参数可以用哪些方式配置？

### 操作

常见方案有：

```text
1. 编译期宏
2. 串口命令
3. Flash 参数保存
4. 上位机协议配置
```

当前阶段选择串口命令。

### 观察

串口命令实现简单，不依赖文件系统和 Flash 擦写，也方便课堂和面试演示。

### 结论

本项目先做“RAM 运行时配置”，断电后恢复默认值。后续如果需要长期保存，可以再增加 Flash 参数区。

## 面试回答

问题：为什么要做运行时阈值配置？

30秒回答：

我把告警阈值从固定宏改成了运行时配置，PC 可以通过 XCOM 串口命令查询和修改阈值。这样调试近距离告警、温湿度告警和电位器告警时，不需要频繁改代码和烧录。控制模块每次处理传感器消息时读取一份配置快照，再根据快照判断 EventGroup 告警位。

深入解释：

配置数据由 `AppControlConfig` 管理，命令任务只负责解析命令和调用 setter，真正的告警判断仍然留在 `AppControl_UpdateFromSensor()`。这样串口命令和业务策略解耦，后续如果换成 Flash 保存或上位机协议下发，也不需要重写传感器采集任务。

项目结合：

例如我可以发送 `SET NEAR 80`，把 VL53L0X 的近距离告警阈值从 120mm 改成 80mm。下一次 VL53L0X 数据进入汇总任务后，控制模块会用新阈值刷新 `ALERT_NEAR_BIT`。

追问：

为什么修改配置要保护临界区？

回答：

因为命令任务可能在写配置，传感器汇总任务可能同时读配置。配置结构体虽然很小，但为了避免读到一半新一半旧的数据，读写时用临界区做一个很短的保护。这个临界区只复制几个整数，不会明显影响实时性。

## 测试清单

正常测试：

```text
XCOM 发送 CONFIG，确认打印默认阈值。
XCOM 发送 SET NEAR 80，确认配置被修改。
靠近 VL53L0X，观察 ALERT near 是否变化。
```

边界测试：

```text
SET POT 100
SET POT 0
SET NEAR 10
SET NEAR 2000
```

异常测试：

```text
SET NEAR ABC
SET NEAR 0
SET POT 101
SET MOTION 0
```

预期打印 `CMD SET failed`。

性能测试：

观察 `HEALTH OK`、`HEARTBEAT`、`STACK` 和传感器日志是否继续正常输出，确认命令解析没有阻塞采集任务。
