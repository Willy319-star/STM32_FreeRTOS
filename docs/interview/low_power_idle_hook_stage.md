# FreeRTOS IdleHook 与低功耗观测阶段

## 阶段目标

本阶段的目标不是直接进入复杂的 STOP 深度低功耗模式，而是先在当前多任务系统中建立一个可靠、可观测、可面试表达的低功耗基础：

- 当所有业务任务都阻塞或延时等待时，让 CPU 进入等待中断状态；
- 通过串口日志证明系统确实有空闲时间；
- 证明项目不是靠死循环忙等运行，而是利用 FreeRTOS 的任务阻塞和 IdleHook 机制。

当前项目里已经有多个周期任务：

- DHT11 温湿度采集任务；
- MPU6050 姿态采集任务；
- VL53L0X 测距任务；
- 电位器 ADC 采集任务；
- ST7735S 显示刷新任务；
- UART 协议发送任务；
- 系统监控任务。

这些任务大多数时间都处于 `vTaskDelayUntil()`、队列等待或周期阻塞状态。当没有普通任务需要运行时，FreeRTOS 会调度空闲任务，也就是 Idle Task。本项目在 IdleHook 中执行 `__WFI()`，让 Cortex-M4 等待下一次中断唤醒。

## 问题 → 操作 → 观察 → 结论

### 问题

如何证明 FreeRTOS 系统在任务空闲时没有一直空转，而是真的进入了低功耗等待状态？

### 操作

在 `FreeRTOSConfig.h` 中开启：

```c
#define configUSE_IDLE_HOOK 1
```

然后实现 `vApplicationIdleHook()`。

本项目里的 IdleHook 只做两件事：

1. 调用 `AppPower_IdleHookNotify()`，给空闲次数计数；
2. 执行 `__WFI()`，让 CPU 进入等待中断状态。

代码逻辑可以理解为：

```c
void vApplicationIdleHook(void)
{
    AppPower_IdleHookNotify();
    __WFI();
}
```

系统监控任务每 5 秒读取并清零一次计数值，然后通过串口打印：

```text
POWER idle_hook=xxxxx wfi=enabled period_ms=5000
```

### 观察

例如串口中出现：

```text
POWER idle_hook=883 wfi=enabled period_ms=5000
```

含义是：

- `period_ms=5000`：统计周期是 5000ms，也就是 5 秒；
- `idle_hook=883`：这 5 秒内，FreeRTOS 空闲任务进入 IdleHook 883 次；
- `wfi=enabled`：每次进入 IdleHook 时都会执行 `__WFI()`；
- 这说明系统在这 5 秒内有很多时间没有业务任务要运行，CPU 可以进入等待中断状态。

### 结论

这条日志说明当前系统不是一直忙等，而是：

- 传感器任务采集完后会阻塞等待下一周期；
- UART、显示、系统监控等任务也会周期性让出 CPU；
- 当所有任务都暂时不需要运行时，FreeRTOS 会运行 Idle Task；
- IdleHook 中执行 `__WFI()`，CPU 等待 SysTick 或外设中断唤醒。

这属于一种安全的轻量级低功耗方式。它不是 STOP 深睡眠，但适合当前项目阶段，因为 UART、I2C、ADC、屏幕刷新和调试输出都还能保持稳定。

## 为什么不一开始就做 STOP 模式？

STOP 模式比 `WFI` Sleep 更省电，但复杂度也高很多：

- STOP 唤醒后通常要恢复系统时钟；
- UART 波特率依赖时钟，时钟恢复不对会导致串口乱码；
- I2C、ADC 等外设可能需要重新初始化；
- 屏幕刷新和串口调试会变得更难定位问题；
- 当前项目仍然处于多传感器实时采集和调试阶段，优先保证系统稳定更重要。

所以本项目先采用 IdleHook + `__WFI()`，这是一个更稳妥的低功耗起点。

## 为什么 IdleHook 里不能直接打印 UART？

IdleHook 属于 FreeRTOS 的空闲任务钩子函数，它有一个重要限制：

**不能在 IdleHook 中执行可能阻塞的操作。**

UART 打印可能会：

- 等待发送完成；
- 等待 UART 互斥锁；
- 占用较长时间；
- 影响空闲任务正常运行。

所以本项目没有在 IdleHook 中直接打印日志，而是只做一个非常轻量的计数。真正的串口打印放到系统监控任务里完成。

这个设计更符合 RTOS 工程习惯。

## 面试回答版本

问题：你这个项目里低功耗是怎么做的？

30 秒回答：

我的项目没有一开始就直接进入 STOP 深睡眠，而是先基于 FreeRTOS IdleHook 做了一个安全的低功耗入口。各个传感器任务都使用 `vTaskDelayUntil()` 或队列阻塞，任务没事做时调度器会进入 Idle Task。我开启了 `configUSE_IDLE_HOOK`，在 `vApplicationIdleHook()` 中执行 `__WFI()`，让 Cortex-M4 等待下一次中断唤醒。同时我加了一个 idle 计数器，系统监控任务每 5 秒打印一次 `POWER idle_hook=xxx`，用串口证明系统确实存在空闲时间，而不是一直忙等。

深入解释：

`__WFI()` 的意思是 Wait For Interrupt。CPU 执行到这里后会暂停运行，直到 SysTick、外设中断或调试事件唤醒它。FreeRTOS 中很多任务都是周期性运行的，比如 DHT11 两秒采一次，VL53L0X 五百毫秒采一次，MPU6050 一百毫秒采一次。它们采集完之后会阻塞等待下一周期，因此 CPU 中间会有空闲窗口。IdleHook 正好可以利用这些空闲窗口做低功耗处理。

项目结合：

在我的项目里，串口能看到类似：

```text
POWER idle_hook=883 wfi=enabled period_ms=5000
```

这说明 5 秒内系统进入了 883 次 IdleHook，并且每次都有机会执行 `WFI`。这就是我低功耗设计的观测证据。

## 常见追问

### 问题：`idle_hook=883` 越大越好吗？

不一定。

它越大，说明系统空闲机会越多；但它不是绝对功耗值。因为真正功耗还和主频、外设开启情况、屏幕背光、电源模块、电路设计有关。

在本项目中，它主要用来证明：

- 任务没有忙等；
- 调度器能进入空闲任务；
- `WFI` 有机会执行。

### 问题：`WFI` 和 STOP 模式有什么区别？

`WFI` 是等待中断指令，通常用于 Sleep 模式，CPU 暂停执行，但系统时钟和外设状态比较容易保持。

STOP 模式更省电，但唤醒后通常需要恢复系统时钟和部分外设状态，复杂度更高。

本项目目前选择 `WFI`，是因为它稳定、可观测、不会破坏 UART 和传感器调试流程。

### 问题：为什么这个功能体现了 FreeRTOS 的优势？

裸机程序里如果没有设计好状态机，很容易在主循环里一直轮询传感器或等待标志位，造成 CPU 忙等。

FreeRTOS 中每个任务都可以在没有工作时主动阻塞，例如：

- `vTaskDelayUntil()`；
- `xQueueReceive()`；
- `xSemaphoreTake()`。

当所有任务都阻塞时，CPU 自动进入 Idle Task，再由 IdleHook 执行低功耗操作。这让任务调度和低功耗入口结合得更自然。

IdleHook 就是 FreeRTOS 在系统没活干的时候，自动调用的用户函数。