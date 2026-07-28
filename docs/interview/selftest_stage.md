# 系统自检 SelfTest 阶段

## 问题

项目已经有多个外设和多个 FreeRTOS 任务，但面试或演示时不能只说“我看到串口一直在打印，所以系统正常”。

更好的方式是提供一个明确的自检入口：

```text
PC 发送 SELFTEST
MCU 基于统一系统快照检查各模块状态
串口输出每个模块 PASS / FAIL
```

这样可以快速回答：

```text
DHT11 是否可用？
MPU6050 是否可用？
VL53L0X 是否真的测距成功？
POT 是否有 ADC 数据？
系统剩余 heap 是否足够？
当前告警位是多少？
```

## 操作

本阶段在 `CommandTask` 中新增命令：

```text
SELFTEST
TEST
```

命令处理函数：

```c
static void execute_selftest(void)
```

实现流程：

```text
XCOM 发送 SELFTEST
    -> USART1 RX IRQ 收字节
    -> CommandTask 拼成一行命令
    -> execute_selftest()
    -> AppSnapshot_Get()
    -> 按模块判断 PASS / FAIL
    -> UART 输出结果
```

## 为什么基于统一快照做自检

自检没有直接读取 DHT11、MPU6050、VL53L0X、POT 的内部变量，而是读取：

```c
AppSystemSnapshot snapshot;
AppSnapshot_Get(&snapshot);
```

原因是：

```text
1. 自检只关心系统当前状态，不关心每个驱动怎么实现
2. 统一快照已经由 TaskSensorLog 汇总更新
3. LCD、STATUS、SELFTEST 都使用同一份状态来源
4. 避免 CommandTask 和各个传感器任务强耦合
```

这也是统一快照模块的一个实际应用场景。

## 自检项目

当前自检包含：

```text
DHT    dht_valid 是否为 1
MPU    mpu_valid 是否为 1
VL53   vl53_valid && vl53_range_valid 是否为 1
POT    pot_valid 是否为 1
HEAP   heap_free 和 heap_min 是否都大于 4096 字节
ALERT  打印当前 alert_bits
```

其中 VL53L0X 要求更严格：

```text
vl53_valid = 1
vl53_range_valid = 1
```

因为 `ONLINE` 只能证明设备在 I2C 上可识别，`range_valid` 才能证明已经得到有效测距。

## 观察

XCOM 发送：

```text
SELFTEST
```

正常情况下应该看到类似：

```text
SELFTEST begin: using AppSystemSnapshot
SELFTEST DHT   PASS temp=25C hum=58% stale=0
SELFTEST MPU   PASS acc=-950,1,-404 motion=0
SELFTEST VL53  PASS range=1 dist=88mm
SELFTEST POT   PASS raw=1106 percent=27%
SELFTEST HEAP  PASS heap=12440 min=12440
SELFTEST ALERT PASS bits=0x0000
SELFTEST RESULT PASS
```

如果某个模块异常，例如 VL53L0X 没有有效距离，可能看到：

```text
SELFTEST VL53  FAIL range=0 dist=0mm
SELFTEST RESULT FAIL
```

## 结论

SelfTest 的意义是把“系统是否可用”变成一条可重复执行的验证命令。

它不是替代详细日志，而是提供一个快速判断入口：

```text
STATUS    看当前状态细节
SELFTEST  看模块是否通过基本健康检查
LOG       控制输出量
FAULT     验证看门狗异常恢复
```

## 面试回答

问题：你的项目如何验证多个外设都正常工作？

30 秒回答：

我做了一个 `SELFTEST` 串口命令。PC 通过 XCOM 发送 `SELFTEST` 后，CommandTask 不直接访问各个传感器驱动，而是读取统一系统状态快照 `AppSystemSnapshot`。然后分别检查 DHT11、MPU6050、VL53L0X、ADC 电位器和 heap 状态，并输出每项 `PASS/FAIL`。这样可以快速证明系统当前是否具备基本运行能力，也能体现多任务系统中的状态汇总和模块解耦设计。

追问：为什么 VL53L0X 自检要求 `range_valid`，而不是只看 `vl53_valid`？

回答：

因为 VL53L0X 的 `vl53_valid` 只能证明设备在线或初始化基本成功，不能证明业务层已经拿到有效距离。项目之前就遇到过 `ONLINE` 但 `RANGE_PENDING` 的情况，所以自检里要求 `vl53_valid && vl53_range_valid`，这样才能证明测距数据真的可用。

## 测试方法

正常测试：

```text
等待系统运行几秒
XCOM 发送 SELFTEST
观察是否 SELFTEST RESULT PASS
```

异常测试：

```text
拔掉某个传感器
发送 SELFTEST
观察对应模块是否 FAIL
```

恢复测试：

```text
恢复传感器接线
等待几个采样周期
再次发送 SELFTEST
观察是否恢复 PASS
```
# 自检是怎么实现的

## 问题

`SELFTEST` 的目标不是重新读一遍所有传感器，而是基于系统已经汇总好的状态，快速判断当前系统是否可用。

如果自检函数直接调用 DHT11、MPU6050、VL53L0X、ADC 的底层读取函数，会带来几个问题：

```text
1. 自检会绕过原来的任务调度和滤波流程
2. CommandTask 会和各个驱动强耦合
3. 可能和正在运行的传感器任务抢 I2C 或 GPIO
4. 自检结果和 LCD / STATUS 看到的状态来源不一致
```

所以当前项目的自检是基于统一快照 `AppSystemSnapshot` 实现的。

## 操作

实现入口在 `app_command.c`：

```c
static void execute_selftest(void)
```

命令解析流程：

```text
XCOM 输入 SELFTEST 或 TEST
    -> USART1 RX 中断接收字节
    -> xQueueSendFromISR() 送到命令队列
    -> CommandTask 拼成完整命令
    -> execute_command()
    -> execute_selftest()
```

命令分支：

```c
} else if (strcmp(cmd, "SELFTEST") == 0 || strcmp(cmd, "TEST") == 0) {
    execute_selftest();
}
```

## 核心步骤

第一步，读取统一系统快照：

```c
AppSystemSnapshot snapshot;

if (!AppSnapshot_Get(&snapshot)) {
    AppObserve_WriteLine("SELFTEST FAIL snapshot unavailable\r\n");
    return;
}
```

这里没有直接访问传感器任务内部变量，而是读取 `AppSnapshot_Get()` 返回的一份稳定副本。

第二步，逐项判断模块状态：

```text
DHT:
    pass = snapshot.dht_valid

MPU:
    pass = snapshot.mpu_valid

VL53:
    pass = snapshot.vl53_valid && snapshot.vl53_range_valid

POT:
    pass = snapshot.pot_valid

HEAP:
    pass = snapshot.heap_free >= 4096 && snapshot.heap_min >= 4096
```

第三步，统一打印结果：

```c
static void print_selftest_result(const char *name,
                                  uint8_t pass,
                                  const char *detail)
```

输出格式：

```text
SELFTEST DHT   PASS temp=25C hum=58% stale=0
SELFTEST VL53  PASS range=1 dist=88mm
SELFTEST RESULT PASS
```

## 为什么 VL53L0X 要判断两个条件

VL53L0X 自检不是只看：

```text
vl53_valid
```

而是判断：

```text
vl53_valid && vl53_range_valid
```

原因是之前调试中出现过：

```text
VL53L0X ONLINE ... status=range-pending
```

这说明设备能被 I2C 识别，但还没有有效距离。自检要证明“测距业务可用”，所以必须要求 `range_valid=1`。

## 为什么 HEAP 阈值用 4096

当前自检里：

```c
snapshot.heap_free >= 4096UL && snapshot.heap_min >= 4096UL
```

这不是精确的产品阈值，而是一个项目阶段的安全下限。它表示系统至少还有 4KB 以上 FreeRTOS heap，避免出现内存几乎耗尽但外设还在勉强运行的情况。

## 观察

正常情况下：

```text
SELFTEST begin: using AppSystemSnapshot
SELFTEST DHT   PASS ...
SELFTEST MPU   PASS ...
SELFTEST VL53  PASS ...
SELFTEST POT   PASS ...
SELFTEST HEAP  PASS ...
SELFTEST ALERT PASS ...
SELFTEST RESULT PASS
```

如果某个模块失败：

```text
SELFTEST VL53  FAIL range=0 dist=0mm
SELFTEST RESULT FAIL
```

## 结论

SelfTest 的实现可以总结成：

```text
命令入口：CommandTask
状态来源：AppSystemSnapshot
判断方式：按模块检查 valid/range/heap
输出方式：AppObserve_WriteLine()
设计目标：快速验证系统基本可用性
```

它复用了统一快照，没有破坏传感器任务的采集节奏，也避免了 CommandTask 和底层驱动耦合。

## 面试回答

问题：你的自检命令是怎么实现的？

30 秒回答：

我的 `SELFTEST` 命令是在 `CommandTask` 里实现的。PC 通过 XCOM 发送 `SELFTEST` 后，串口接收中断只负责收字节并投递到命令队列，真正解析在 CommandTask 中完成。自检函数不会直接调用各个传感器驱动，而是通过 `AppSnapshot_Get()` 获取统一系统快照，然后检查 DHT11、MPU6050、VL53L0X、ADC 和 heap 状态，逐项输出 `PASS/FAIL`。这样既能快速验证系统可用性，也保持了模块解耦。
