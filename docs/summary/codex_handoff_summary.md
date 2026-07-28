# Codex 协作交接总结

本文档用于在更换电脑、更换 Codex 会话、重新拉取 Git 仓库后，快速恢复当前项目上下文。

项目仓库：

```text
D:\STM32_FreeRTOS
https://github.com/Willy319-star/STM32_FreeRTOS.git
```

当前主项目名称：

```text
STM32F407_FreeRTOS_multisensors
```

当前应用目录：

```text
app/STM32F407_FreeRTOS_multisensors
```

重要原则：

```text
1. 用户希望 Codex 像嵌入式项目导师一样工作，不只是改代码，还要解释为什么。
2. 遇到问题时按“问题 -> 操作 -> 观察 -> 结论”讲清楚。
3. 每完成一个重要阶段，要同步到 docs/interview 或 docs/summary。
4. 编译成功后，如果用户说“烧录”，或之后明确要求“编译完直接烧录”，就直接烧录。
5. 不要随便删除测试例程，app/baremetal_blink 要保留。
6. 不要把个人简历 Word 等敏感文件默认提交到 Git；但用户明确要求上传的 docx 可以单独 git add -f。
```

## 1. 当前项目定位

这是一个基于 STM32F407VET6 天空星开发板和 FreeRTOS 的多传感器实时采集节点。

它不是单纯点亮 LED 或读取一个传感器，而是围绕 FreeRTOS 多任务工程能力完成：

```text
多任务传感器采集
任务间 Queue 通信
EventGroup 告警状态
Mutex 保护共享资源
统一系统状态快照
ST7735S 屏幕显示
UART 命令行
UART 二进制协议帧
运行时参数配置
Flash 掉电保存
日志分级
SELFTEST 自检
IWDG 任务健康监测
IdleHook + WFI 低功耗入口
故障注入
```

这个项目已经可以写进简历，简历项目名称建议为：

```text
基于 STM32F407 + FreeRTOS 的多传感器实时采集节点
```

## 2. 硬件平台和接线

开发板：

```text
STM32F407VET6 天空星
主频 168MHz
Flash 512KB
SRAM 128KB
```

串口：

```text
UART1_TX -> PA9
UART1_RX -> PA10
串口参数：115200, 8 data bits, 1 stop bit, no parity
常用串口助手：XCOM
```

传感器和外设：

```text
DHT11 DATA      -> PC0
MPU6050 SCL     -> PB6
MPU6050 SDA     -> PB7
VL53L0X SCL     -> PB10
VL53L0X SDA     -> PB11
电位器 ADC      -> PA0 / ADC1_IN0
ST7735S SCL     -> PA5
ST7735S SDA     -> PA7
ST7735S RES     -> PC4
ST7735S DC      -> PC5
ST7735S CS      -> PC6
ST7735S BLK     -> 3V3
```

注意：

```text
1. 用户用的是 0.96 寸 ST7735S 屏幕，不是 OLED。
2. 屏幕曾经出现黑屏、花屏、方向错误、字体太大、残影等问题，最后通过纯色测试、横屏布局、清屏区域和坐标偏移逐步解决。
3. MPU6050 实际 WHOAMI 读到 0x74，但寄存器兼容，项目按兼容 MPU6050 读取。
4. VL53L0X 地址是 0x29，调试时一定区分 ONLINE 和 RANGE_OK。
```

## 3. 当前代码结构

核心应用目录：

```text
app/STM32F407_FreeRTOS_multisensors
```

主要文件：

```text
main.c             FreeRTOS 入口和任务初始化
app_sensor.c       DHT11、MPU6050、VL53L0X、POT、LCD、日志汇总任务
app_observe.c      UART 日志、日志分级、任务注册、栈水位输出
app_command.c      UART 命令行解析
app_comm.c         通信发送任务
app_protocol.c     二进制协议帧封装
app_control.c      EventGroup 告警、运行时配置、Flash 保存
app_snapshot.c     统一系统状态快照
app_health.c       IWDG 健康监测和故障注入
app_power.c        IdleHook/WFI 低功耗统计
```

BSP：

```text
bsp/src/board.c
bsp/src/bsp_debug_uart.c
bsp/src/bsp_i2c.c
bsp/src/bsp_adc.c
bsp/src/bsp_time.c
```

驱动：

```text
drivers/src/dht11.c
drivers/src/mpu6050.c
drivers/src/vl53l0x.c
drivers/src/st7735s_tft.c
```

文档：

```text
docs/interview/    每个阶段的学习和面试复习材料
docs/summary/      项目总结、交接总结、简历和面试材料
```

## 4. FreeRTOS 任务设计

当前系统包含这些任务：

```text
HEART      心跳和 heap 输出
WORKER     证明调度存活
SYSMON     heap、min_heap、task count、stack high water、idle_hook 监控
CMD        UART 命令解析
DHT11      DHT11 温湿度采集
MPU        MPU6050 数据采集和运动/姿态变化判断
VL53       VL53L0X 测距
POT        ADC 电位器采集和滤波
LCD        ST7735S 屏幕刷新
SLOG       传感器消息汇总、日志、快照、告警、通信分发
COMMTX     二进制协议帧发送
CONTROL    EventGroup 告警输出
HEALTH     任务健康监测和 IWDG 喂狗
```

任务优先级思路：

```text
采集任务和 CMD 任务优先级略高，因为它们是输入源。
显示、日志、通信、监控任务优先级略低，因为它们是消费端，可接受轻微滞后。
周期任务尽量使用 vTaskDelayUntil，避免长期周期漂移。
```

## 5. 任务间通信设计

当前用了四种核心机制：

### Queue

传感器任务通过 Queue 发送结构体消息：

```text
DHT11/MPU/VL53/POT
    -> AppMessage
    -> SensorLog/SLOG
```

选择 Queue 的原因：

```text
适合生产者-消费者模型
能传完整结构体
多个传感器任务可以发给同一个汇总任务
比全局变量更清晰
比 Task Notification 更适合多生产者和复杂数据
```

### EventGroup

用于告警状态：

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

核心解释：

```text
Queue 传数据，EventGroup 表示状态位。
```

### Mutex

保护共享资源：

```text
UART 输出
I2C 总线
系统快照
运行时配置
```

### ISR -> Queue

UART RX 中断只收字节并投递队列：

```text
USART1_IRQHandler()
    -> AppCommand_OnRxByteFromISR()
    -> xQueueSendFromISR()
    -> CommandTask 里拼接和解析命令
```

中断里不做字符串解析、不打印日志、不执行复杂逻辑。

## 6. UART 命令

在 XCOM 中发送文本命令。

常用命令：

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

注意：

```text
1. 如果 XCOM 出现乱码，先确认是不是 BIN ON 打开了二进制协议帧。
2. 二进制帧适合用 scripts/uart_protocol_parser.py 解析，不适合直接在 XCOM 里看。
3. 如果只是人工观察，建议 BIN OFF，LOG INFO 或 LOG WARN。
```

## 7. 当前重要调试结论

### DHT11

已解决的问题：

```text
DATA 最终接到 PC0 后成功。
之前出现 checksum、idle-low、no-response-low 等问题。
当前策略：读成功更新值；读失败保留上一次有效值，并记录 err/stale。
```

面试解释：

```text
DHT11 是单总线时序敏感传感器，偶发失败正常。保留上一次有效值可以避免显示和告警因单次失败误判，但必须保留错误计数说明数据质量。
```

### MPU6050

已解决的问题：

```text
I2C1 scan 能看到 0x68。
WHOAMI@68=0x74。
寄存器兼容，按 MPU6050 兼容器件读取。
现在能输出 acc/gyro，并增加 motion/posture 判断。
```

### VL53L0X

这是调试中最复杂的外设。

出现过：

```text
addr-nack
model=0x00
range-pending
spad-info-fail
ref-calibration-fail
```

最终结果：

```text
VL53L0X RANGE_OK tick=... addr=0x29 model=0xEE dist=...mm filtered=1 err=0
```

修复方向：

```text
I2C bus recovery
SCL/SDA 电平诊断
软件 I2C 延时放慢
恢复 VL53L0X 寄存器页
修正 SPAD ready 判断
修正 reference calibration 顺序和状态位判断
加入滑动平均滤波
```

面试重点：

```text
ONLINE 不等于 RANGE_OK。
ONLINE 只说明设备在线或 model id 可读。
RANGE_OK 才说明业务层拿到了有效距离。
```

### ADC 电位器

实现：

```text
PA0 / ADC1_IN0
raw -> percent
平均滤波
死区去抖
控制 ST7735S 页面切换
```

### ST7735S

屏幕最终使用：

```text
0.96 寸 ST7735S
黑底白字
横屏
显示传感器数据
电位器切换页面
```

调试过程：

```text
黑屏 -> 纯色测试 -> 花屏/方向错误 -> 横屏配置 -> 字体过大 -> 清屏残影 -> 页面布局优化
```

## 8. 可靠性功能

### 日志分级

当前等级：

```text
ERROR
WARN
INFO
DEBUG
```

已删除旧命令：

```text
LOG QUIET
LOG NORMAL
```

### 系统快照

核心文件：

```text
app_snapshot.c
app_snapshot.h
```

用途：

```text
LCD、STATUS、SELFTEST 不直接访问各个传感器任务，而是读取统一 AppSystemSnapshot。
降低模块耦合，统一数据来源。
```

### SELFTEST

命令：

```text
SELFTEST
TEST
```

检查：

```text
DHT
MPU
VL53
POT
HEAP
ALERT
```

### Flash 配置保存

命令：

```text
SET ...
SAVE CONFIG
LOAD CONFIG
DEFAULT CONFIG
```

设计：

```text
SET 只改 RAM。
SAVE CONFIG 才写 Flash，避免频繁擦写。
Flash 记录包含 magic/version/config/crc。
linker 已预留配置扇区，避免程序覆盖配置区。
```

### IWDG 健康监测

核心思想：

```text
不要让每个任务自己喂狗。
关键任务上报心跳。
HealthMonitor 统一判断所有关键任务健康后才喂 IWDG。
如果注入故障或任务超时，停止喂狗，等待复位。
```

命令：

```text
HEALTH STATUS
FAULT MPU
FAULT VL53
FAULT DHT11
FAULT CLEAR
```

### 低功耗

当前实现：

```text
FreeRTOS IdleHook -> __WFI()
```

说明：

```text
这是安全的低功耗入口。
暂时没有做 STOP 深睡眠，因为 STOP 会影响 UART、I2C、时钟恢复和传感器时序。
```

## 9. 常用开发命令

编译：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

烧录：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\flash.ps1
```

手动 CMake：

```powershell
cmake -S . -B build -G Ninja -DAPP=STM32F407_FreeRTOS_multisensors
cmake --build build
```

裸机测试例程：

```powershell
cmake -S . -B build -G Ninja -DAPP=baremetal_blink
cmake --build build
```

Git 同步：

```powershell
git status --short
git add .
git commit -m "message"
git push origin main
```

注意：

```text
*.docx 已被 .gitignore 忽略。
如果用户明确要求上传某个 Word，需要 git add -f 指定文件。
```

## 10. 已经上传到 Git 的重要提交

主项目提交：

```text
7836ad1 Add STM32F407 FreeRTOS multisensor project
```

面试问答 Word 提交：

```text
3be3c7d Add interview QA document
```

当前远端：

```text
origin/main
http://github.com/Willy319-star/STM32_FreeRTOS.git
```

## 11. 已生成的重要文档

项目总览：

```text
docs/summary/project_summary.md
```

面试问答：

```text
docs/summary/STM32F407_FreeRTOS_multisensors_interview_QA.docx
```

阶段文档：

```text
docs/interview/freertos_observe_framework.md
docs/interview/dht11_queue_stage.md
docs/interview/mpu6050_i2c_stage.md
docs/interview/vl53l0x_i2c_stage.md
docs/interview/pot_adc_stage.md
docs/interview/st7735s_tft_stage.md
docs/interview/uart_protocol_stage.md
docs/interview/uart_line_command_stage.md
docs/interview/eventgroup_alert_stage.md
docs/interview/system_monitor_stage.md
docs/interview/task_priority_stage.md
docs/interview/iwdg_health_monitor_stage.md
docs/interview/runtime_config_stage.md
docs/interview/flash_config_persistence_stage.md
docs/interview/iwdg_fault_injection_stage.md
docs/interview/sensor_filter_fusion_stage.md
docs/interview/log_level_stage.md
docs/interview/system_snapshot_stage.md
docs/interview/selftest_stage.md
```

## 12. 如果换新设备，建议恢复流程

1. 拉取仓库：

```powershell
git clone https://github.com/Willy319-star/STM32_FreeRTOS.git
cd STM32_FreeRTOS
```

2. 打开这些文档：

```text
README.md
docs/summary/project_summary.md
docs/summary/codex_handoff_summary.md
docs/summary/STM32F407_FreeRTOS_multisensors_interview_QA.docx
```

3. 检查工具链：

```text
arm-none-eabi-gcc
cmake
ninja
openocd
CMSIS-DAP
```

4. 编译：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

5. 连接硬件和串口：

```text
COMx
115200
8N1
```

6. 烧录：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\flash.ps1
```

7. 串口验证：

```text
STATUS
SELFTEST
CONFIG
LOG STATUS
```

## 13. 后续可优化方向

当前项目已经具备简历展示完整度。

后续如果还要增强，建议优先级：

```text
1. 统一错误码和故障统计。
2. PC 解析脚本增加 CSV 保存。
3. 协议帧增加完整系统快照类型。
4. 启动后自动执行一次 SELFTEST，并缓存结果。
5. UART 发送改 DMA。
6. 更完整的低功耗策略，但不要过早做 STOP 深睡眠。
```

暂时不建议投入太多时间的方向：

```text
复杂姿态解算
大规模驱动重构
深度低功耗模式
过度美化屏幕 UI
```

## 14. 给下一个 Codex 的工作方式提醒

如果用户说“开始下一步”，不要直接盲目加功能。

先判断当前阶段是否已经验证：

```text
问题 -> 操作 -> 观察 -> 结论
```

如果是代码任务：

```text
1. 先读相关文件。
2. 说明要改什么和为什么。
3. 用 apply_patch 修改。
4. 编译。
5. 用户要求或已有习惯需要时烧录。
6. 同步 docs/interview 或 docs/summary。
```

如果是调试任务：

```text
1. 先看串口日志。
2. 判断是接线、电源、时序、任务调度、配置还是代码问题。
3. 一次只改一个变量。
4. 用 UART 输出证据。
5. 把最终结论写进对应 markdown。
```

如果是简历或面试任务：

```text
1. 优先基于真实项目内容。
2. 不夸大没有实现的功能。
3. 强调 FreeRTOS、通信、可靠性、调试闭环。
4. 给出 30 秒、1 分钟、3 分钟项目介绍。
```

