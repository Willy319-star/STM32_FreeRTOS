# IWDG 故障注入验证阶段

## 这一阶段做了什么

本阶段给 HealthMonitor 增加“可控故障注入”命令，用来验证 IWDG 是否真的能在任务异常时复位系统。

命令在 XCOM 中发送：

```text
FAULT MPU
FAULT VL53
FAULT DHT11
FAULT STATUS
FAULT CLEAR
HEALTH STATUS
LOG QUIET
LOG NORMAL
```

## 问题

为什么已经写了 IWDG，还要做故障注入？

## 操作

只看到下面这种日志，不能完全证明看门狗链路是有效的：

```text
HEALTH OK checked=5 iwdg=fed
```

它只能说明正常情况下会喂狗。

要证明看门狗设计有效，还需要验证异常路径：

```text
某个任务异常
        ↓
HealthMonitor 发现异常
        ↓
停止喂 IWDG
        ↓
IWDG 超时复位 MCU
        ↓
重启后打印 previous reset was IWDG
```

本阶段通过 XCOM 命令模拟某个任务失效。

## 观察

发送：

```text
FAULT MPU
```

会看到：

```text
CMD FAULT injected task=MPU. IWDG will reset if not cleared in about 6s
CMD FAULT mask=0x0020
HEALTH STATUS tick=... iwdg_started=1 checks=... fails=... fault_mask=0x0020 report_mask=...
```

随后 HealthMonitor 会打印：

```text
HEALTH FAIL task=MPU reason=injected now=... fault_mask=0x0040
HEALTH IWDG feed skipped, waiting for reset if fault persists
```

如果不发送 `FAULT CLEAR`，大约 6 秒后 MCU 会被 IWDG 复位。

重启后如果看到：

```text
BOOT RESET CAUSE: IWDG
HEALTH boot evidence: previous reset was IWDG
```

说明看门狗复位路径验证成功。

## 日志太多时如何验证

### 问题

传感器、心跳、栈监控日志很多，可能会漏看 IWDG 复位证据。

### 操作

测试前先在 XCOM 发送：

```text
LOG QUIET
HEALTH STATUS
FAULT MPU
```

它会抑制一部分周期性普通日志，例如：

```text
HEARTBEAT
WORKER alive
SYS
POWER
STACK
DHT11 OK
MPU6050 OK
VL53L0X RANGE_OK
POT OK
HEALTH OK
```

但会保留关键日志，例如：

```text
CMD ...
HEALTH FAIL ...
HEALTH IWDG feed skipped ...
HEALTH STATUS ...
BOOT RESET CAUSE: IWDG
HEALTH boot evidence ...
```

测试结束后可以发送：

```text
LOG NORMAL
```

恢复完整日志。

### 观察

现在 IWDG 复位证据会打印两次：

```text
BOOT RESET CAUSE: IWDG
```

这是 `main()` 早期打印，出现在传感器任务大量输出之前。

```text
HEALTH boot evidence: previous reset was IWDG
```

这是 `HealthMonitorTask` 启动后打印。

### 结论

如果日志太多，应该先降低日志噪声，再做故障注入测试。

早期启动日志比任务日志更适合记录复位原因，因为它在 FreeRTOS 调度器启动前就已经输出。

## 如果没有看到复位信息怎么办

### 问题

发送 `FAULT MPU` 后只看到 `CMD FAULT mask=...`，但没有看到 IWDG 复位信息。

### 操作

先在 XCOM 发送：

```text
HEALTH STATUS
```

重点看：

```text
iwdg_started
checks
fails
fault_mask
```

含义：

```text
iwdg_started=1  表示 IWDG 已经启动
checks 增加      表示 HealthMonitor 任务正在运行
fails 增加       表示 HealthMonitor 已经检测到故障
fault_mask 非 0  表示故障注入标记存在
```

### 观察

如果 `fault_mask=0x0020`，说明 `FAULT MPU` 已经生效。

如果 `fails` 增加，说明 HealthMonitor 已经停止喂狗。

此时继续等待，理论上 IWDG 会复位 MCU。

### 结论

看门狗验证不能只看 `FAULT` 命令是否收到，还要看 HealthMonitor 是否运行、IWDG 是否启动、fail 计数是否增加。

本项目通过 `HEALTH STATUS` 把这几个关键证据打印出来，方便定位问题。

## 结论

故障注入验证的是“异常恢复能力”，不是普通功能是否能跑。

它让项目从：

```text
我写了看门狗
```

变成：

```text
我验证过任务异常时系统会停止喂狗并自动复位
```

这在面试里更有说服力。

## 为什么采用软故障注入

### 问题

为什么不真的让某个任务进入死循环？

### 操作

真实死循环测试风险更大：

```text
任务可能长期占用 CPU
串口可能无法继续输入
系统可能来不及打印故障证据
调试过程不可控
```

当前方案是在 HealthMonitor 内部增加一个 `s_injected_fault_mask`。

当发送：

```text
FAULT MPU
```

HealthMonitor 会把 MPU 当成异常任务处理，即使 MPU 任务本身还在正常运行。

### 观察

这种方式可以稳定观察到：

```text
命令收到
故障标记设置
HealthMonitor 发现故障
停止喂狗
IWDG 复位
启动后识别 IWDG reset flag
```

### 结论

软故障注入更适合学习和面试演示。

它验证的是健康监测和看门狗链路，而不是故意把系统弄到不可控状态。

## 如何取消故障

### 问题

如果误发了 `FAULT MPU` 怎么办？

### 操作

在 IWDG 超时复位前，通过 XCOM 发送：

```text
FAULT CLEAR
```

### 观察

应该看到：

```text
CMD FAULT CLEAR OK
CMD FAULT mask=0x0000
```

之后 HealthMonitor 会恢复：

```text
HEALTH OK checked=... iwdg=fed
```

### 结论

`FAULT CLEAR` 只在复位前有效。

如果 IWDG 已经复位，系统会重新启动，故障注入标记本来就是 RAM 变量，也会自动清零。

## 面试回答

问题：你怎么验证看门狗真的有效？

30秒回答：

我给 HealthMonitor 做了一个软故障注入机制，可以通过 XCOM 发送 `FAULT MPU` 模拟 MPU 任务失效。HealthMonitor 检测到注入故障后停止喂 IWDG，如果 6 秒内不清除故障，IWDG 会复位 MCU。重启后系统会检查 RCC 的 IWDG reset flag，并打印 `previous reset was IWDG`，这样就能闭环证明看门狗异常恢复链路有效。

深入解释：

我没有直接让任务进入死循环，因为那样可能导致串口不可用、日志来不及输出，测试过程不可控。软故障注入只影响 HealthMonitor 的判断结果，既能验证停止喂狗和 IWDG 复位，也能保留串口日志证据。

追问：

为什么故障注入标记不用保存到 Flash？

回答：

故障注入只是测试状态，应该只存在 RAM 中。IWDG 复位后这个状态自动清零，系统可以正常恢复。如果把故障注入状态保存到 Flash，复位后可能再次进入故障测试，反而影响恢复。
