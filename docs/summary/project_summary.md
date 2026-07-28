# STM32F407_FreeRTOS_multisensors 项目总结

## 1. 项目定位

本项目是一个基于 STM32F407VET6 和 FreeRTOS 的多传感器实时采集、显示、告警与通信节点。

项目接入 DHT11、MPU6050、VL53L0X、ADC 电位器和 ST7735S 显示屏，围绕 FreeRTOS 多任务系统完成：

```text
传感器周期采集
数据滤波和状态判断
统一系统状态快照
LCD 实时显示
UART 命令交互
二进制协议帧上报
EventGroup 告警
运行时参数配置
Flash 掉电保存
IWDG 健康监测和故障恢复
IdleHook 低功耗入口
日志分级和系统自检
```

这个项目的重点不是“点亮某个外设”，而是把多个嵌入式常见能力组合成一个可以解释、可以测试、可以扩展的 FreeRTOS 工程。

## 2. 硬件组成

开发板：

```text
STM32F407VET6 天空星
主频 168MHz
Flash 512KB
SRAM 128KB
```

外设：

```text
DHT11       温湿度传感器
MPU6050     三轴加速度/角速度传感器
VL53L0X     激光测距模块
ST7735S     0.96 寸 TFT 屏幕
ADC 电位器  旋钮输入
UART1       调试和命令接口
```

主要接线：

```text
DHT11 DATA  -> PC0
MPU6050     -> PB6/PB7 软件 I2C1
VL53L0X     -> PB10/PB11 软件 I2C2
POT ADC     -> PA0 / ADC1_IN0
ST7735S     -> PA5/PA7/PC4/PC5/PC6
UART1       -> PA9/PA10
```

## 3. 软件架构

项目采用分层结构：

```text
Application
    app_command
    app_sensor
    app_snapshot
    app_control
    app_comm
    app_observe
    app_health
    app_power

Drivers
    dht11
    mpu6050
    vl53l0x
    st7735s_tft

BSP
    board
    bsp_debug_uart
    bsp_i2c
    bsp_adc
    bsp_time

RTOS / HAL
    FreeRTOS
    STM32 HAL
    CMSIS
```

设计目标：

```text
传感器驱动只关心外设读写
传感器任务只关心周期采集
汇总任务负责统一状态和输出
控制模块负责告警和配置
通信模块负责协议帧
命令模块负责 PC 到 MCU 的控制入口
健康模块负责任务状态和看门狗
```

## 4. FreeRTOS 任务设计

当前任务包括：

```text
HEART      系统心跳
WORKER     调度存活证明
SYSMON     heap/stack/idle 监控
CMD        串口命令解析
DHT11      温湿度采集
MPU        IMU 数据采集
VL53       激光测距
POT        ADC 电位器采集
LCD        屏幕刷新
SLOG       传感器消息汇总
COMMTX     二进制协议帧发送
CONTROL    EventGroup 告警输出
HEALTH     任务健康监测和 IWDG 喂狗
```

优先级设计：

```text
tskIDLE_PRIORITY + 2:
    传感器采集任务
    串口命令任务

tskIDLE_PRIORITY + 1:
    显示、日志、通信、系统监控、告警、健康监测
```

原因：

```text
采集任务和命令任务是输入源，需要较及时响应
显示、日志、协议发送是消费者，允许轻微滞后
所有周期任务使用 vTaskDelayUntil 或 Queue 阻塞主动让出 CPU
```

## 5. 任务间通信设计

项目中使用了多种 FreeRTOS 通信机制：

### Queue

传感器任务通过 Queue 发送结构体消息：

```text
DHT11 / MPU / VL53 / POT
    -> AppMessage
    -> s_sensor_queue
    -> TaskSensorLog
```

选择 Queue 的原因：

```text
能传递完整结构体
适合生产者-消费者模型
能天然缓冲突发消息
比全局变量更清楚、更安全
```

### Mutex

用于保护共享资源：

```text
UART 输出
I2C1 总线
I2C2 总线
系统状态快照
运行时配置
```

### EventGroup

用于表达告警状态位：

```text
HOT
DRY
NEAR
POT_HIGH
MOTION
DHT_FAIL
MPU_FAIL
VL53_FAIL
POT_FAIL
```

EventGroup 适合表达“状态是否成立”，不适合传递温度、距离这类复杂数据。

### ISR -> Queue

UART RX 中断只收一个字节，然后：

```text
xQueueSendFromISR()
    -> CommandTask
```

中断里不解析命令、不打印日志，避免 ISR 时间过长。

## 6. 外设驱动和调试重点

### DHT11

特点：

```text
单总线时序敏感
采样周期较慢
偶发读取失败正常
```

处理方式：

```text
读取成功：更新最新温湿度
读取失败：如果之前有有效值，则保留上一次有效值，并标记 stale
```

面试点：

```text
为什么保留上一次有效值？
为什么还要打印 stale？
为什么 DHT11 不适合高频读取？
```

### MPU6050

特点：

```text
I2C 寄存器读写
WHO_AM_I 用于设备识别
数据频率高于其他传感器
```

处理方式：

```text
100ms 周期读取加速度和角速度
根据 gyro 阈值判断 motion
根据加速度变化判断 posture_changed
```

面试点：

```text
I2C 设备如何确认在线？
为什么 WHO_AM_I=0x74 仍可按兼容 MPU6050 处理？
为什么不直接上复杂姿态解算？
```

### VL53L0X

这是项目中调试最有价值的外设。

遇到的问题：

```text
addr-nack
model=0x00
range-pending
spad-info-fail
ref-calibration-fail
```

解决方式：

```text
增加 I2C bus recovery
增加 SCL/SDA 电平诊断
软件 I2C 延时从 5us 放慢到 8us
恢复 VL53L0X 内部寄存器默认页
修正 SPAD ready 判断
修正 reference calibration 顺序和状态位判断
增加滑动平均滤波
```

最终结果：

```text
VL53L0X RANGE_OK ... model=0xEE dist=...mm filtered=1 err=0
```

面试点：

```text
ONLINE 为什么不等于 RANGE_OK？
SPAD、tuning、calibration、range start 分别是什么？
软件 I2C 和硬件 I2C 的区别是什么？
如何定位 addr-nack？
```

### ADC 电位器

处理方式：

```text
ADC1_IN0 / PA0 采集 raw
4 点平均滤波
8 raw 死区去抖
转换为百分比
控制 LCD 页面切换
```

面试点：

```text
为什么 ADC 要滤波？
为什么页面切换需要去抖？
```

### ST7735S

处理方式：

```text
SPI-like GPIO 控制屏幕
黑底白字显示
横屏布局
电位器切换页面
读取 AppSystemSnapshot 刷新显示
```

调试经历：

```text
曾出现黑屏、花屏、方向错误、字体过大、残影
通过纯色测试、重新初始化、清屏、坐标偏移和页面布局逐步解决
```

面试点：

```text
为什么先做纯色测试？
为什么显示任务不直接读取传感器任务内部变量？
```

## 7. 统一系统状态快照

新增：

```text
app_snapshot.c
app_snapshot.h
```

核心结构：

```text
AppSystemSnapshot
```

数据流：

```text
传感器任务
    -> Queue
        -> TaskSensorLog
            -> AppSnapshot_UpdateFromSensor()

LCD / STATUS / SELFTEST
    -> AppSnapshot_Get()
```

作用：

```text
降低模块耦合
统一系统状态来源
避免 LCD、STATUS、SELFTEST 分别拼数据
读取时复制一份快照，避免长时间占用 mutex
```

面试回答：

> 我没有让显示和命令模块直接访问各个传感器任务，而是设计了统一系统状态快照。传感器任务通过 Queue 上报消息，汇总任务更新快照，LCD、STATUS、SELFTEST 都读取这份快照。这样模块之间耦合更低，状态来源也更一致。

## 8. UART 通信设计

项目中 UART 分成两条线：

### 文本命令

PC 到 MCU：

```text
USART1 RX IRQ
    -> xQueueSendFromISR()
    -> CommandTask
    -> 行命令解析
```

支持命令：

```text
STATUS
SELFTEST
ALERT
CONFIG
SET ...
SAVE CONFIG
LOAD CONFIG
BIN ON/OFF
LOG ...
HEALTH STATUS
FAULT ...
```

### 二进制协议帧

MCU 到 PC：

```text
AppMessage
    -> CommMessage
    -> CommTxTask
    -> AppProtocol_BuildFrame()
    -> UART
```

帧结构：

```text
SOF
version
type
seq
len
tick
payload
crc16
```

意义：

```text
XCOM 文本日志适合人看
二进制协议帧适合 PC 程序解析
两者分离，便于后续接 Linux 网关或上位机
```

## 9. 运行时配置和 Flash 保存

运行时配置：

```text
SET NEAR <mm>
SET HOT <C>
SET DRY <%>
SET POT <%>
SET MOTION <dps>
```

配置结构：

```text
AppControlConfig
```

Flash 保存：

```text
SAVE CONFIG
LOAD CONFIG
DEFAULT CONFIG
```

Flash 记录包含：

```text
magic
version
config
crc
```

设计原因：

```text
RAM 配置用于快速调试
Flash 配置用于确认后的掉电保存
不在每次 SET 后自动写 Flash，避免频繁擦写
启动时校验 magic/version/CRC，不可信则回退默认值
```

## 10. EventGroup 告警

告警来源：

```text
温度过高
湿度过低
距离过近
电位器过高
检测到运动
外设失效
```

EventGroup 的价值：

```text
用 bit 表示状态
多个状态可以组合
适合告警任务、STATUS、SELFTEST 查询
比 Queue 更适合表达状态位
```

面试回答：

> Queue 用来传数据，EventGroup 用来表示状态。传感器的温湿度和距离通过 Queue 传递，告警状态通过 EventGroup 保存，这样数据流和状态流职责分开。

## 11. 日志分级

当前日志等级：

```text
ERROR
WARN
INFO
DEBUG
```

命令：

```text
LOG ERROR
LOG WARN
LOG INFO
LOG DEBUG
LOG STATUS
```

实现：

```text
AppObserve_WriteLine()
    -> infer_log_level()
    -> should_suppress()
    -> UART mutex
    -> BSP_DebugUART_WriteString()
```

意义：

```text
正常调试用 INFO
长时间运行用 WARN
故障复现用 ERROR/WARN
需要二进制协议解析时减少文本干扰
```

## 12. 系统自检

命令：

```text
SELFTEST
TEST
```

实现：

```text
CommandTask
    -> execute_selftest()
    -> AppSnapshot_Get()
    -> 检查 DHT/MPU/VL53/POT/HEAP/ALERT
    -> 输出 PASS/FAIL
```

典型输出：

```text
SELFTEST DHT   PASS ...
SELFTEST MPU   PASS ...
SELFTEST VL53  PASS ...
SELFTEST POT   PASS ...
SELFTEST HEAP  PASS ...
SELFTEST RESULT PASS
```

意义：

```text
一条命令快速验证系统基本可用性
证明统一快照可以服务多个模块
适合演示和面试现场验证
```

## 13. IWDG 看门狗和健康监测

设计原则：

```text
不让每个任务直接喂狗
所有关键任务先 AppHealth_Report()
HealthMonitor 统一检查
只有所有任务健康才喂 IWDG
```

为什么这样设计：

```text
如果某个普通任务还在喂狗，可能掩盖其他关键任务卡死
统一健康监测能避免“局部健康掩盖整体异常”
```

故障注入：

```text
FAULT MPU
FAULT VL53
FAULT DHT11
FAULT CLEAR
HEALTH STATUS
```

验证闭环：

```text
注入故障
HealthMonitor 检测失败
停止喂 IWDG
IWDG 复位 MCU
启动后打印 IWDG reset evidence
```

## 14. 低功耗

当前实现：

```text
FreeRTOS IdleHook
    -> __WFI()
```

同时系统监控输出：

```text
POWER idle_hook=... wfi=enabled
```

设计原因：

```text
当前项目先实现安全的空闲低功耗入口
不直接进入 STOP 深睡眠，避免影响 UART、I2C、传感器时序
通过 idle_hook 计数证明系统不是一直忙等
```

面试回答：

> 我没有一开始就做 STOP 深睡眠，而是先基于 FreeRTOS IdleHook 做 WFI。各任务大部分时间阻塞在 delay 或 queue 上，当系统空闲时进入 IdleTask，再由 IdleHook 执行 WFI。这样低功耗入口和任务调度结合自然，也更安全。

## 15. 内存和栈监控

系统监控任务周期输出：

```text
SYS heap=...
STACK DHT11 high_water=...
STACK MPU high_water=...
...
```

意义：

```text
观察 FreeRTOS heap 是否泄漏
观察每个任务栈余量
为调整任务栈大小提供证据
```

面试点：

```text
heap 是 FreeRTOS 动态内存
stack high water mark 是历史最小剩余栈空间
数值越小越危险
```

## 16. 项目调试方法总结

本项目调试方式可以总结为：

```text
先验证最小硬件
再验证单个外设
再接入 FreeRTOS 任务
再加 Queue 和日志
再加告警、协议、配置、看门狗
每一步都通过 UART 输出证据
```

典型调试案例：

```text
DHT11 checksum / idle-low
MPU WHOAMI=0x74
VL53L0X addr-nack / model=0x00 / spad-info-fail / ref-calibration-fail
ST7735S 黑屏、花屏、方向错误
UART 二进制帧在 XCOM 显示乱码
IWDG 故障注入未看到复位证据
```

核心方法：

```text
现象
可能原因
证据
验证方法
修复方向
```

## 17. 项目亮点总结

简历可以写：

```text
基于 STM32F407 + FreeRTOS 设计多传感器实时采集与告警节点，
接入 DHT11、MPU6050、VL53L0X、ADC 电位器和 ST7735S 显示屏，
实现多任务采集、Queue 消息通信、EventGroup 告警、UART 协议帧、
运行时参数配置、Flash 掉电保存、IWDG 健康监测、IdleHook 低功耗、
日志分级、统一系统快照和 SELFTEST 自检。
```

更精简的简历 bullet：

```text
- 基于 STM32F407 + FreeRTOS 实现多传感器实时采集节点，完成 DHT11、MPU6050、VL53L0X、ADC 和 ST7735S 显示屏驱动接入。
- 使用 FreeRTOS Queue、EventGroup、mutex 实现任务间通信、告警状态管理和共享资源保护，设计统一系统状态快照供 LCD、STATUS、SELFTEST 复用。
- 设计 UART 命令行和二进制协议帧，支持运行时阈值配置、日志分级、PC 解析和 Flash 掉电保存。
- 引入 IWDG 看门狗和任务健康监测机制，只有关键任务健康时统一喂狗，并通过故障注入验证异常复位链路。
- 针对 VL53L0X 完成 I2C 在线识别、SPAD/tuning/calibration/range 流程调试，解决 addr-nack、model=0x00 和 range-pending 等问题。
```

## 18. 面试 1 分钟讲法

> 我做的是一个基于 STM32F407 和 FreeRTOS 的多传感器实时采集与告警节点。硬件上接入了 DHT11、MPU6050、VL53L0X、ADC 电位器和 ST7735S 屏幕。软件上我把不同外设拆成独立采集任务，通过 FreeRTOS Queue 把数据发送给汇总任务，再更新统一系统状态快照。LCD、串口 STATUS 和 SELFTEST 都读取这份快照，降低了模块耦合。项目还实现了 EventGroup 告警、UART 命令行、二进制协议帧、运行时参数配置、Flash 掉电保存、日志分级、IdleHook 低功耗和 IWDG 健康监测。调试中比较有代表性的是 VL53L0X，我通过 I2C scan、Model ID、SPAD、calibration、range start 分阶段定位，最终实现稳定 RANGE_OK。

## 19. 面试 3 分钟讲法

> 这个项目的目标不是单独驱动某个传感器，而是做一个比较完整的 FreeRTOS 多任务系统。  
>  
> 在任务设计上，我把 DHT11、MPU6050、VL53L0X 和 ADC 分别放在独立采集任务里，按照不同采样周期运行。采集结果封装成 `AppMessage`，通过 FreeRTOS Queue 发送给汇总任务。汇总任务负责更新统一系统快照、输出日志、更新 EventGroup 告警，并把数据提交给通信模块。这样传感器任务不直接操作 LCD、UART 或告警逻辑，模块边界比较清楚。  
>  
> 在通信上，我做了两套 UART 能力：一套是文本命令，用 USART1 RX 中断收字节，通过 Queue 交给 CommandTask 解析，支持 STATUS、SELFTEST、CONFIG、SET、LOG、FAULT 等命令；另一套是二进制协议帧，用 type、seq、len、tick、payload、CRC16 组织数据，方便 PC 程序解析。  
>  
> 在可靠性上，我做了运行时阈值配置和 Flash 掉电保存，Flash 记录带 magic、version 和 CRC，启动时校验失败会回退默认值。看门狗方面，我没有让每个任务自己喂狗，而是让关键任务定期上报心跳，HealthMonitor 统一判断所有任务健康后才喂 IWDG。如果某个任务异常，就停止喂狗并最终复位。为了验证这条链路，我还做了 FAULT 命令进行故障注入。  
>  
> 调试中比较典型的是 VL53L0X。它不是简单读一个寄存器就能拿到距离，需要完成 SPAD、tuning、reference calibration 和 range start。我遇到过 addr-nack、model=0x00、spad-info-fail 和 ref-calibration-fail，通过增加 I2C bus recovery、SCL/SDA 诊断、寄存器页恢复和状态位判断修正，最后实现稳定测距。这个过程体现了分层验证和可观测性设计。

## 20. 面试高频追问

### 为什么用 Queue？

Queue 适合传递完整数据结构，也符合生产者-消费者模型。传感器任务是生产者，汇总任务是消费者。相比全局变量，Queue 更清晰；相比 EventGroup，Queue 能传具体数据；相比 Task Notification，Queue 更适合多生产者。

### 为什么还要 EventGroup？

Queue 传数据，EventGroup 表示状态。温度、距离、IMU 数据通过 Queue 传；告警是否触发用 EventGroup bit 表示。

### 为什么需要统一快照？

LCD、STATUS、SELFTEST、协议上报都需要当前系统状态。如果它们分别访问各个传感器模块，会造成高耦合。统一快照让状态来源一致，读取时复制一份副本，安全且清晰。

### 为什么不在中断里解析 UART 命令？

UART 是字节流，中断里只应该做短操作。项目中 ISR 只收字节并投递到 Queue，真正的命令拼接和解析在 CommandTask 中完成，避免 ISR 阻塞和复杂逻辑。

### 为什么 IWDG 由 HealthMonitor 统一喂？

如果每个任务都能喂狗，可能某个任务正常喂狗而掩盖其他任务卡死。统一 HealthMonitor 检查所有关键任务心跳，只有全部健康才喂狗，更能反映系统整体健康。

### 为什么 Flash 保存不在每次 SET 后自动执行？

Flash 擦写有寿命限制，而且擦除以扇区为单位。SET 是调试过程中的频繁操作，所以只改 RAM。用户确认后发送 SAVE CONFIG 才写 Flash。

### 为什么软件 I2C？

当前项目是学习和调试项目，软件 I2C 引脚灵活，方便做 bus recovery 和观察 ACK。正式产品中如果引脚允许，会优先选择硬件 I2C，提高时序稳定性和 CPU 效率。

### 为什么 VL53L0X 自检要求 range_valid？

VL53 valid 只能证明设备在线或初始化基本成功，range_valid 才证明业务层拿到了有效距离。项目之前出现过 ONLINE 但 RANGE_PENDING，所以 SelfTest 要检查 range_valid。

## 21. 当前项目可继续优化方向

适合简历前收尾：

```text
保持 README 和 project_summary.md 最新
保留 SELFTEST 作为验收命令
演示前先 LOG WARN，再 SELFTEST
```

可选增强：

```text
统一错误码枚举
上位机解析脚本生成 CSV
开机自动执行一次 SelfTest 并缓存结果
协议帧增加系统快照类型
```

暂不建议继续扩大的方向：

```text
复杂姿态解算
深度 STOP 低功耗
大规模驱动重构
```

当前项目已经具备写进简历的完整度，下一步更重要的是准备演示流程和面试讲法。
