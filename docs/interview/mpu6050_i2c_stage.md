# MPU6050 I2C stage

## 技术原理

MPU6050 通过 I2C 总线通信。本阶段先实现 I2C1 软件总线：

- SCL: PB6
- SDA: PB7

启动时先做 I2C scan，用 ACK 证明总线上有哪些设备在线，然后读取 MPU6050 的
`WHO_AM_I` 寄存器确认设备身份。当前驱动兼容标准 `0x68` 和部分兼容芯片返回的
`0x74`。

## 为什么先做 I2C 基础

I2C 是后续 MPU6050 和 VL53L0X 的基础。如果不先证明总线能扫描到地址，直接调
传感器驱动会把接线、地址、寄存器配置和任务调度问题混在一起。

## 技术方案比较

### 方案 A：STM32 硬件 I2C

优点：

- 性能更好。
- CPU 占用更低。
- 后续更适合 DMA 或中断方式。

缺点：

- 初始化配置更复杂。
- 调试早期容易混入时钟、复用功能、ACK、BUSY 状态等问题。

### 方案 B：GPIO 软件 I2C

优点：

- 引脚和时序直观。
- 便于通过日志验证 I2C scan 和寄存器读写。
- 适合作为当前项目的 MVP。

缺点：

- CPU 占用更高。
- 速度不如硬件 I2C。
- 不适合高吞吐或复杂实时场景。

当前选择：

本阶段选择软件 I2C，因为目标是先验证 PB6/PB7 接线、I2C ACK、WHO_AM_I 和数据
读取。后续如果需要更高性能，再替换成硬件 I2C。

## 为什么需要 I2C mutex

当前只有 MPU6050 使用 I2C1，但后续可能会加入更多 I2C 设备。I2C 总线是共享
资源，不能让多个任务同时 start、write、read、stop。使用 mutex 可以保证一次
完整 I2C 事务不会被其他任务打断。

## 当前数据流

```text
TaskMPU6050
  -> xSemaphoreTake(s_i2c1_mutex)
  -> MPU6050_ReadData()
  -> xSemaphoreGive(s_i2c1_mutex)
  -> xQueueSend(AppMessage)
  -> TaskSensorLog
  -> UART
```

## 高频面试题

### 问题：为什么先读取 WHO_AM_I？

30 秒回答：

因为 I2C scan 只能证明某个地址有 ACK，不能证明它一定是 MPU6050，也不能证明
寄存器读写流程正确。读取 `WHO_AM_I` 可以进一步确认设备身份和寄存器访问链路。

深入解释：

I2C scan 只是发送地址并检查 ACK。它无法区分同地址的不同设备，也无法验证后续
寄存器读写是否正确。`WHO_AM_I` 是 MPU6050 的身份寄存器，标准值通常是 `0x68`。
我这个项目中兼容 `0x74`，是因为部分 MPU 兼容模块的寄存器布局相同但 ID 不同。

项目结合：

启动后日志会输出：

```text
I2C1 scan: 0x68
MPU6050 init OK addr=0x68 whoami=0x68
```

或者兼容模块：

```text
MPU6050 init OK addr=0x68 whoami=0x74
```

追问：

- I2C scan 到地址是否等于驱动完成？
- 如果 scan 有 0x68，但 WHO_AM_I 读失败，可能是什么原因？
- 如果 WHO_AM_I 是 0x74，为什么代码仍然允许继续？

### 问题：为什么 MPU6050 数据也走 Queue？

30 秒回答：

DHT11 和 MPU6050 都是传感器生产者。为了统一数据通路，MPU6050 任务也把采样
结果封装成 `AppMessage` 发到同一个 Queue，由日志任务统一消费。这样后续加入
VL53L0X 和 ADC 时，只需要扩展消息类型，不需要重写通信框架。

深入解释：

Queue 表示生产者-消费者模型。多个传感器任务可以拥有不同采样周期，但都使用
同一种消息格式上报数据。消费者根据 `msg.type` 区分 DHT11、MPU6050 等来源。

## 测试设计

正常测试：

- MPU6050 VCC/GND 正确连接。
- PB6 接 SCL，PB7 接 SDA。
- 串口观察 `I2C1 scan` 是否出现 `0x68` 或 `0x69`。
- 观察 `MPU6050 OK` 是否周期输出。

边界测试：

- 长时间运行 5 分钟，观察 heap 是否稳定。
- 摇动模块，观察 acc/gyro 数值是否变化。

异常测试：

- 拔掉 SCL，观察 `MPU6050 FAIL`，Heartbeat 和 DHT11 是否继续运行。
- 拔掉 SDA，观察 I2C scan 是否不再出现 `0x68`。
- 断开 MPU6050 VCC，观察是否只影响 MPU6050。

性能测试：

- 观察 MPU6050 的 tick 间隔是否约为 100ms。
- 观察 DHT11 2 秒周期和 Heartbeat 1 秒周期是否仍然稳定。
