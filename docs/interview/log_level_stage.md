# 日志分级阶段

## 问题

项目运行到多传感器、多任务、多协议阶段后，串口打印会越来越多。如果所有日志都直接输出到 XCOM，会出现两个问题：

```text
1. 正常周期日志太多，真正的错误容易被淹没
2. 打开二进制协议或高频传感器日志时，调试体验很差
```

所以需要做日志分级，让系统可以按调试目的选择输出量。

## 操作

本阶段在 `app_observe` 中增加统一日志等级：

```text
ERROR  只看严重错误
WARN   看告警、故障、命令响应
INFO   默认等级，显示正常运行状态
DEBUG  调试等级，预留给更详细的内部信息
```

串口命令：

```text
LOG ERROR
LOG WARN
LOG INFO
LOG DEBUG
LOG STATUS
```

旧命令 `LOG QUIET` 和 `LOG NORMAL` 已删除。
当前只保留明确的日志等级命令。

实现方式：

```text
各任务仍然调用 AppObserve_WriteLine()
    -> app_observe 根据日志内容推断等级
    -> 和当前 s_log_level 比较
    -> 等级不够重要则过滤
    -> 通过 UART mutex 输出
```

## 观察

默认 `LOG INFO` 时，可以看到：

```text
DHT11 OK ...
MPU6050 OK ...
VL53L0X RANGE_OK ...
POT OK ...
HEARTBEAT ...
SYS ...
```

发送 `LOG WARN` 后，正常周期日志会减少，主要保留：

```text
CMD ...
FAULT ...
ALERT 非零状态
STALE
queue full
FAIL / ERROR
```

发送 `LOG ERROR` 后，只保留严重错误，例如：

```text
VL53L0X FAIL ...
DHT11 FAIL ...
IWDG feed skipped ...
BOOT RESET CAUSE ...
```

发送 `LOG STATUS` 可以查看当前等级：

```text
CMD LOG INFO
```

## 结论

日志分级的意义不是“少打印”，而是让系统在不同阶段有不同的可观测粒度：

```text
调试驱动时用 INFO/DEBUG
长时间运行时用 WARN
故障复现时用 ERROR/WARN
需要 PC 解析二进制帧时减少文本干扰
```

这样既保留了可观测性，又避免 UART 被无关日志刷屏。

## 为什么放在 AppObserve

方案 A：每个任务自己判断日志等级。

优点：

```text
每条日志等级最准确
```

缺点：

```text
每个任务都要重复写判断逻辑
后续维护分散
容易出现某些模块忘记分级
```

方案 B：统一放在 `AppObserve` 出口过滤。

优点：

```text
改动范围小
所有 UART 文本日志统一经过一个出口
可以兼容原来的 AppObserve_WriteLine()
```

缺点：

```text
部分日志等级需要根据字符串推断，精度不如显式传参
```

当前选择方案 B，因为项目已经有大量 `AppObserve_WriteLine()`，集中改动更稳。后续如果需要更精确，可以逐步把关键日志改成：

```c
AppObserve_WriteLineLevel(APP_LOG_WARN, "...");
```

## 面试回答

问题：你的项目为什么要做日志分级？

30 秒回答：

项目里有 DHT11、MPU6050、VL53L0X、ADC、电源、看门狗、协议帧等多个模块，如果所有日志都按同一等级输出，正常周期日志会淹没故障信息，也会影响串口协议调试。所以我在 `AppObserve` 里做了统一日志分级，支持 `ERROR/WARN/INFO/DEBUG`。默认 `INFO` 用于正常调试，`WARN` 用于长时间运行时只看告警和命令响应，`ERROR` 用于只观察严重故障。这样既保证系统可观测，又能控制串口输出量。

追问：为什么不让每个任务自己控制日志？

回答：

每个任务自己控制虽然更精确，但会导致重复逻辑分散在各个模块里，后续维护成本高。本项目选择把日志出口统一放在 `AppObserve`，因为所有文本日志本来都经过 UART mutex 输出。这样改动范围小，也符合“观测能力集中管理”的思路。对于特别关键的日志，后续可以使用 `AppObserve_WriteLineLevel()` 显式指定等级。

## 测试方法

正常测试：

```text
XCOM 发送 LOG STATUS
观察返回 CMD LOG INFO / WARN / ERROR / DEBUG
```

等级切换测试：

```text
发送 LOG WARN
观察 HEARTBEAT / DHT11 OK / MPU6050 OK 是否减少

发送 LOG INFO
观察周期日志是否恢复
```

异常测试：

```text
发送 FAULT MPU
观察 LOG WARN / LOG ERROR 下是否仍能看到故障信息
```

协议测试：

```text
保持 BIN OFF 时用 XCOM 看文本
需要 PC 解析二进制帧时，先 LOG WARN 减少文本干扰
```
# 日志分级是如何实现的

## 问题

日志分级不是简单写几个 `if` 少打印一点，而是要解决一个工程问题：

```text
多个任务都在打印日志
    -> UART 是共享资源
    -> 日志数量越来越多
    -> 需要按重要程度过滤
    -> 还要能通过串口命令动态切换
```

所以这个项目把日志分级放在统一出口 `AppObserve` 中实现。

## 操作

整个实现链路是：

```text
任务产生日志
    -> AppObserve_WriteLine()
    -> infer_log_level()
    -> should_suppress()
    -> UART mutex
    -> BSP_DebugUART_WriteString()
```

也就是说，DHT11、MPU6050、VL53L0X、POT、Health、Command 等任务不直接操作 UART，而是统一调用：

```c
AppObserve_WriteLine("VL53L0X RANGE_OK ...\r\n");
```

然后 `AppObserve` 决定这条日志到底要不要输出。

## 1. 定义日志等级

在 `app_observe.h` 中定义：

```c
typedef enum {
    APP_LOG_ERROR = 0,
    APP_LOG_WARN = 1,
    APP_LOG_INFO = 2,
    APP_LOG_DEBUG = 3,
} AppLogLevel;
```

这里数字越小，表示日志越重要：

```text
ERROR  最重要
WARN
INFO
DEBUG  最详细
```

当前系统默认等级是：

```c
static volatile AppLogLevel s_log_level = APP_LOG_INFO;
```

所以默认情况下，`ERROR/WARN/INFO` 都会输出，`DEBUG` 会被过滤。

## 2. 设置当前日志等级

通过这个函数设置：

```c
void AppObserve_SetLogLevel(AppLogLevel level)
{
    if (level > APP_LOG_DEBUG) {
        level = APP_LOG_INFO;
    }
    s_log_level = level;
}
```

这里做了一个保护：如果传入非法等级，就恢复为 `INFO`，避免系统进入不可预期状态。

查询当前等级：

```c
AppLogLevel AppObserve_GetLogLevel(void)
```

把等级转成字符串：

```c
const char *AppObserve_LogLevelName(AppLogLevel level)
```

所以串口里才能打印：

```text
CMD LOG INFO
CMD LOG WARN
```

## 3. 自动判断一条日志属于哪个等级

为了兼容原来大量已有代码，项目没有强制每条日志都改成：

```c
AppObserve_WriteLineLevel(APP_LOG_WARN, "...");
```

而是保留原来的：

```c
AppObserve_WriteLine("...");
```

然后在 `infer_log_level()` 里根据字符串内容推断日志等级。

例如：

```c
if (strstr(text, "FAIL") != NULL ||
    strstr(text, "ERROR") != NULL ||
    strstr(text, "IWDG feed skipped") != NULL ||
    strstr(text, "BOOT RESET CAUSE") != NULL) {
    return APP_LOG_ERROR;
}
```

含义是：

```text
只要日志里包含 FAIL / ERROR / IWDG feed skipped / BOOT RESET CAUSE
就认为它是 ERROR 级别
```

再比如：

```c
if (strstr(text, "STALE") != NULL ||
    strstr(text, "queue full") != NULL ||
    strstr(text, "FAULT") != NULL ||
    strncmp(text, "CMD", 3U) == 0) {
    return APP_LOG_WARN;
}
```

含义是：

```text
STALE、queue full、FAULT、CMD 这类信息属于 WARN
```

正常周期日志，例如：

```text
HEARTBEAT ...
DHT11 OK ...
MPU6050 OK ...
VL53L0X RANGE_OK ...
POT OK ...
SYS ...
STACK ...
```

会被归为 `INFO`。

## 4. 判断是否过滤

过滤逻辑在：

```c
static uint8_t should_suppress(AppLogLevel level)
{
    return (level > s_log_level) ? 1U : 0U;
}
```

因为日志等级数字越小越重要，所以判断规则是：

```text
如果 当前日志等级 > 当前允许等级
    说明这条日志不够重要
    过滤掉
否则
    输出
```

举例，当前设置为：

```text
s_log_level = APP_LOG_WARN
```

那么：

```text
ERROR = 0  输出
WARN  = 1  输出
INFO  = 2  过滤
DEBUG = 3  过滤
```

所以发送 `LOG WARN` 后，正常周期日志会减少，但错误和告警仍然能看到。

## 5. UART 输出仍然加互斥锁

真正输出时：

```c
if (xSemaphoreTake(s_uart_mutex, portMAX_DELAY) == pdTRUE) {
    BSP_DebugUART_WriteString(text);
    xSemaphoreGive(s_uart_mutex);
}
```

这是因为 FreeRTOS 里多个任务都可能同时打印 UART。

如果没有 mutex，两条日志可能交叉输出，变成混乱字符串。日志分级解决的是“是否输出”，UART mutex 解决的是“多个任务输出时不要打架”。

## 6. 串口命令如何控制日志等级

命令解析在 `app_command.c`。

当 XCOM 发送：

```text
LOG WARN
```

代码会执行：

```c
AppObserve_SetLogLevel(APP_LOG_WARN);
print_log_mode();
```

旧命令 `LOG QUIET` 和 `LOG NORMAL` 已删除，避免和新的日志等级命令重复。
现在统一使用：

```text
LOG WARN
LOG INFO
```

## 观察

发送：

```text
LOG WARN
```

会看到：

```text
CMD LOG WARN
```

之后 `HEARTBEAT`、`DHT11 OK`、`MPU6050 OK`、`POT OK` 这类 `INFO` 周期日志会减少。

如果出现：

```text
VL53L0X FAIL ...
DHT11 FAIL ...
FAULT ...
queue full
```

这些仍然会被输出。

## 结论

这个项目的日志分级可以总结为：

```text
统一入口：AppObserve_WriteLine()
自动分类：infer_log_level()
等级过滤：should_suppress()
串口互斥：s_uart_mutex
命令控制：LOG ERROR/WARN/INFO/DEBUG
```

这样做的好处是改动范围小，原来的任务代码基本不用大改，所有日志都经过同一个出口管理。

## 面试回答

问题：你项目里的日志分级是怎么实现的？

30 秒回答：

我把日志分级放在统一观测模块 `AppObserve` 中实现。各个任务不直接操作 UART，而是调用 `AppObserve_WriteLine()`。这个函数会先通过 `infer_log_level()` 根据日志内容判断是 `ERROR/WARN/INFO/DEBUG`，再和当前全局日志等级 `s_log_level` 比较，如果不够重要就过滤掉；如果需要输出，则通过 UART mutex 保护后发送到串口。日志等级可以通过 XCOM 命令动态切换，例如 `LOG WARN`、`LOG INFO`。这样既减少了正常周期日志刷屏，也保证故障日志不会被漏掉。

追问：为什么不用每个任务自己判断日志等级？

回答：

每个任务自己判断会更精确，但逻辑会分散在 DHT11、MPU6050、VL53L0X、Health 等多个模块里，后续维护成本高。本项目所有文本日志本来就统一经过 `AppObserve` 输出，所以我选择在统一出口做过滤。这样改动范围小，也符合“观测能力集中管理”的工程思路。后续对于特别关键的日志，可以逐步改成 `AppObserve_WriteLineLevel()` 显式指定等级。
