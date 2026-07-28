# STM32F407_FreeRTOS_multisensors

基于 STM32F407VET6 天空星开发板和 FreeRTOS 的多传感器实时采集、显示、告警与串口通信节点。

本项目面向嵌入式秋招项目展示，重点不是简单读取传感器，而是把 FreeRTOS 多任务调度、任务间通信、外设驱动、系统可观测性、参数配置、低功耗和可靠性机制串成一个完整工程。

## 项目功能

- DHT11 温湿度采集，支持偶发失败时保留上一次有效值。
- MPU6050/兼容 IMU 采集加速度和角速度，支持简单运动与姿态变化判断。
- VL53L0X 激光测距，支持 I2C 在线识别、初始化、测距、滑动平均滤波和异常恢复。
- ADC 电位器采集，支持平均滤波和死区去抖。
- ST7735S 0.96 寸屏幕显示传感器状态，黑底白字，电位器切换页面。
- UART1 文本命令交互和二进制协议帧上报。
- EventGroup 告警：高温、干燥、近距离、电位器高值、运动、外设失败。
- 运行时阈值配置，支持串口命令修改。
- Flash 参数掉电保存，带 magic/version/CRC 校验。
- IWDG 看门狗和任务健康监测，支持故障注入验证。
- IdleHook + WFI 低功耗入口。
- 日志分级：ERROR/WARN/INFO/DEBUG。
- 统一系统状态快照和 SELFTEST 自检命令。

## 硬件平台

- MCU: STM32F407VET6
- Flash: 512 KB
- SRAM: 128 KB
- HSE: 8 MHz
- System clock: 168 MHz
- RTOS: FreeRTOS
- Toolchain: arm-none-eabi-gcc + CMake + Ninja + OpenOCD
- Debug/Flash: CMSIS-DAP, SWD

## 外设接线

| 外设 | 信号 | STM32 引脚 | 说明 |
| --- | --- | --- | --- |
| UART1 | TX | PA9 | 调试串口发送 |
| UART1 | RX | PA10 | 调试串口接收 |
| DHT11 | DATA | PC0 | 单总线数据 |
| MPU6050 | SCL | PB6 | 软件 I2C1 |
| MPU6050 | SDA | PB7 | 软件 I2C1 |
| VL53L0X | SCL | PB10 | 软件 I2C2 |
| VL53L0X | SDA | PB11 | 软件 I2C2 |
| 电位器 | ADC | PA0 | ADC1_IN0 |
| ST7735S | SCL | PA5 | SPI SCK |
| ST7735S | SDA | PA7 | SPI MOSI |
| ST7735S | RES | PC4 | 复位 |
| ST7735S | DC | PC5 | 数据/命令 |
| ST7735S | CS | PC6 | 片选 |
| ST7735S | BLK | 3V3 | 背光常亮 |

串口参数：

```text
COMx
115200
8 data bits
1 stop bit
No parity
```

## 工程目录

```text
app/STM32F407_FreeRTOS_multisensors  项目业务代码
app/baremetal_blink                  裸机 LED 测试例程，保留用于硬件验证
bsp                                  板级支持包：时钟、UART、ADC、软件 I2C、时间基准
drivers                              外设驱动：DHT11、MPU6050、VL53L0X、ST7735S
config                               HAL 和 FreeRTOS 配置
core                                 中断入口、syscalls
linker                               STM32F407VETx 链接脚本
startup                              启动文件
third_party                          STM32 HAL、CMSIS、FreeRTOS 源码
docs/interview                       项目阶段文档和面试复习材料
scripts                              构建、烧录和串口协议解析脚本
```

## 核心模块

| 模块 | 文件 | 作用 |
| --- | --- | --- |
| 观测框架 | `app_observe.c` | UART 输出、任务注册、栈水位、日志分级 |
| 命令解析 | `app_command.c` | USART1 RX 中断到命令队列，解析行命令 |
| 传感器任务 | `app_sensor.c` | DHT11、MPU6050、VL53L0X、ADC、LCD、汇总日志 |
| 状态快照 | `app_snapshot.c` | 保存系统统一状态，供 LCD/STATUS/SELFTEST 读取 |
| 告警控制 | `app_control.c` | EventGroup 告警、运行时配置、Flash 参数保存 |
| 协议发送 | `app_comm.c` | 将传感器消息转为通信消息并发送协议帧 |
| 协议封装 | `app_protocol.c` | SOF、version、type、seq、len、tick、payload、CRC16 |
| 健康监测 | `app_health.c` | 任务心跳、IWDG 喂狗、故障注入、复位证据 |
| 低功耗 | `app_power.c` | IdleHook 计数与 WFI 入口 |

## FreeRTOS 任务划分

| 任务 | 周期/触发 | 优先级 | 职责 |
| --- | --- | --- | --- |
| HEART | 1000 ms | +1 | 系统心跳和 heap 输出 |
| WORKER | 3000 ms | +1 | 调度存活证明 |
| SYSMON | 5000 ms | +1 | heap、min heap、任务栈水位、IdleHook |
| CMD | UART RX | +2 | 串口命令解析 |
| DHT11 | 2000 ms | +2 | 温湿度采集 |
| MPU | 100 ms | +2 | IMU 采集和运动判断 |
| VL53 | 500 ms | +2 | 激光测距 |
| POT | 500 ms | +2 | ADC 电位器采集与滤波 |
| LCD | 1000 ms | +1 | ST7735S 页面刷新 |
| SLOG | Queue 阻塞 | +1 | 消费传感器消息、更新快照、日志、告警和协议 |
| COMMTX | Queue 阻塞 | +1 | 二进制协议帧发送 |
| CONTROL | 周期/事件 | +1 | 告警状态输出 |
| HEALTH | 1000 ms | +1 | 任务健康检查和 IWDG 喂狗 |

## 任务间通信

- 传感器任务到汇总任务：FreeRTOS Queue，传递 `AppMessage` 结构体。
- 汇总任务到通信任务：FreeRTOS Queue，传递 `CommMessage`。
- 告警状态：FreeRTOS EventGroup。
- 系统状态共享：`AppSystemSnapshot` + mutex。
- UART RX 中断到命令任务：ISR 中 `xQueueSendFromISR()`，任务中解析命令。
- UART 文本输出：`AppObserve` 统一出口 + mutex，避免多任务输出交叉。

## 串口命令

在 XCOM 中发送文本命令：

```text
HELP
STATUS
SELFTEST
ALERT
CONFIG
SET NEAR 120
SET HOT 32
SET DRY 30
SET POT 80
SET MOTION 100
SAVE CONFIG
LOAD CONFIG
DEFAULT CONFIG
BIN ON
BIN OFF
LOG ERROR
LOG WARN
LOG INFO
LOG DEBUG
LOG STATUS
HEALTH STATUS
FAULT MPU
FAULT VL53
FAULT DHT11
FAULT CLEAR
```

常用验证：

```text
STATUS      查看统一系统状态快照
SELFTEST    一键自检各模块 PASS/FAIL
LOG WARN    减少周期日志，只看告警/命令/错误
LOG INFO    恢复默认日志
CONFIG      查看当前告警阈值
```

## 构建

推荐使用项目脚本：

```powershell
.\scripts\build.ps1
```

或手动执行：

```powershell
cmake -S . -B build -G Ninja -DAPP=STM32F407_FreeRTOS_multisensors
cmake --build build
```

输出文件：

```text
build/STM32F407_FreeRTOS_multisensors.elf
build/STM32F407_FreeRTOS_multisensors.hex
build/STM32F407_FreeRTOS_multisensors.bin
build/STM32F407_FreeRTOS_multisensors.map
```

## 烧录

```powershell
.\scripts\flash.ps1
```

或手动执行 OpenOCD：

```powershell
openocd -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg -c "program {D:/STM32_FreeRTOS/build/STM32F407_FreeRTOS_multisensors.elf} verify" -c "reset run" -c "shutdown"
```

## 裸机测试例程

保留 `app/baremetal_blink` 用于最小硬件验证：

```powershell
cmake -S . -B build -G Ninja -DAPP=baremetal_blink
cmake --build build
```

## 推荐测试流程

1. 打开 XCOM，设置 115200 8N1。
2. 复位开发板，观察启动日志。
3. 等待传感器运行几秒。
4. 发送 `STATUS`，确认 `CMD SNAP` 中 DHT、MPU、VL53、POT 有数据。
5. 发送 `SELFTEST`，确认 `SELFTEST RESULT PASS`。
6. 发送 `LOG WARN`，确认周期日志减少。
7. 发送 `SET NEAR 80`，靠近 VL53L0X，观察 EventGroup 告警变化。
8. 发送 `SAVE CONFIG`，复位后发送 `CONFIG` 验证 Flash 参数恢复。
9. 发送 `FAULT MPU`，观察 HealthMonitor 停止喂 IWDG 并触发复位。

## 简历亮点

- 基于 FreeRTOS 实现多任务传感器采集，使用 Queue、EventGroup、mutex 完成任务间通信和资源保护。
- 移植并调试 DHT11、MPU6050、VL53L0X、ST7735S、ADC 电位器等外设。
- 设计统一系统状态快照，降低 LCD、命令、协议、告警模块之间的耦合。
- 实现 UART 命令行和二进制协议帧，支持 PC 查询、配置、协议解析和日志分级。
- 实现运行时阈值配置和内部 Flash 掉电保存，使用 magic/version/CRC 保证参数可信。
- 引入 IWDG 看门狗和任务健康监测，只有关键任务健康时才统一喂狗，并支持故障注入验证复位链路。
- 使用 IdleHook + WFI 实现 FreeRTOS 空闲低功耗入口。

## 面试资料

项目阶段记录和面试复习材料位于：

```text
docs/interview/
```

其中建议重点复习：

```text
project_summary.md
dht11_queue_stage.md
vl53l0x_i2c_stage.md
uart_protocol_stage.md
eventgroup_alert_stage.md
iwdg_health_monitor_stage.md
system_snapshot_stage.md
selftest_stage.md
```
