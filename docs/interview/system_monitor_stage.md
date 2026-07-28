# FreeRTOS system monitor stage

## 阶段目标

问题：
多任务系统跑起来以后，怎么证明系统不是“看起来能跑”，而是真的有资源余量？

操作：
新增系统监控能力：

- `TaskSystemMonitor` 每 5 秒打印一次系统状态。
- `AppObserve_RegisterTask()` 注册各任务句柄。
- 监控任务读取每个任务的 `uxTaskGetStackHighWaterMark()`。
- 同时打印 `xPortGetFreeHeapSize()` 和 `xPortGetMinimumEverFreeHeapSize()`。

观察：
串口会出现类似：

```text
SYS heap=xxxxx min_heap=xxxxx task_count=10
STACK HEART high_water=...
STACK WORKER high_water=...
STACK SYSMON high_water=...
STACK COMMTX high_water=...
STACK DHT11 high_water=...
STACK MPU high_water=...
STACK VL53 high_water=...
STACK POT high_water=...
STACK LCD high_water=...
STACK SLOG high_water=...
```

结论：
这一步的意义是把 FreeRTOS 的运行状态变成可观测数据。面试时不只是说“我给任务分配了栈”，而是能说“我通过 high water mark 观察过每个任务的最小剩余栈，并根据结果判断任务栈是否合理”。

## heap 和 min_heap

问题：
`heap` 和 `min_heap` 分别是什么？

操作：

- `xPortGetFreeHeapSize()`：当前剩余堆空间。
- `xPortGetMinimumEverFreeHeapSize()`：系统启动以来历史最低剩余堆空间。

观察：
如果 `heap` 长期稳定，说明当前动态内存没有持续消耗。
如果 `min_heap` 很低，说明系统运行某个阶段曾经接近堆耗尽。

结论：
`heap` 看当前，`min_heap` 看历史最坏情况。面试时可以说：我更关注 `min_heap`，因为它能反映系统运行过程中的峰值内存压力。

## stack high water mark

问题：
什么是任务栈高水位？

操作：
使用：

```c
uxTaskGetStackHighWaterMark(task_handle)
```

观察：
这个值表示任务运行以来“最少还剩多少栈空间”，单位是 word，不是 byte。STM32F407 是 Cortex-M4，一个 word 通常是 4 字节。

结论：
如果 high water mark 很小，说明任务栈快不够了。比如只剩几十个 word，就要考虑增大该任务 stack depth，或者减少局部大数组和深层函数调用。

## 为什么要注册 TaskHandle_t

问题：
为什么创建任务时要保存 `TaskHandle_t`？

操作：
在 `xTaskCreate()` 的最后一个参数传入句柄地址，例如：

```c
TaskHandle_t dht_handle = NULL;
xTaskCreate(TaskDHT11, "DHT11", 384U, NULL,
            tskIDLE_PRIORITY + 2U, &dht_handle);
AppObserve_RegisterTask("DHT11", dht_handle);
```

观察：
如果没有任务句柄，监控任务就很难准确查询指定任务的栈水位。

结论：
任务句柄是 FreeRTOS 管理任务的引用。保存句柄后，可以查询任务状态、栈水位，后续也可以用于挂起、恢复或删除任务。

## 和 stack overflow hook 的区别

问题：
既然已经开启了 `configCHECK_FOR_STACK_OVERFLOW=2`，为什么还要看 high water mark？

操作：

- Stack overflow hook：事后保护，真的溢出了才进入错误处理。
- High water mark：事前观察，提前知道栈余量是否危险。

观察：
如果等到 hook 触发，系统已经异常了；如果提前观察 high water mark，可以在系统正常运行时调栈大小。

结论：
hook 是底线保护，high water mark 是调试和容量评估工具。工程中两者最好都要有。

## 面试回答：你怎么评估任务栈大小是否合理

30 秒回答：
我不是只凭感觉给任务分配栈，而是在系统运行后用 `uxTaskGetStackHighWaterMark()` 观察每个任务的最小剩余栈。这个值表示任务运行以来栈最紧张时还剩多少 word。如果某个任务 high water mark 很小，说明栈余量不足，需要增大 stack depth 或减少局部大数组。同时我也开启了 stack overflow hook，作为异常兜底。这样可以从运行证据上证明任务栈配置是有依据的。

## 面试回答：你怎么判断系统有没有内存泄漏

30 秒回答：
我会周期性打印当前 heap 和 min_heap。`heap` 是当前剩余堆空间，`min_heap` 是系统启动以来历史最低剩余堆空间。如果系统稳定运行很久后 heap 不持续下降，说明没有明显动态内存泄漏；如果 min_heap 过低，说明某个阶段内存压力较大，需要检查队列、任务栈或动态分配。这个项目里队列和任务基本在启动阶段创建，运行时没有频繁 malloc，所以 heap 应该趋于稳定。
