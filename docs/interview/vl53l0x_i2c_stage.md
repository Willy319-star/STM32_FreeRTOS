# 软件 I2C 和硬件 I2C 的区别

## 问题

VL53L0X 这边为什么会提到“软件 I2C 时序不够稳”？软件 I2C 和硬件 I2C 有什么区别？什么时候应该用硬件 I2C？

## 操作

先把两种方式分清楚：

```text
硬件 I2C：
    使用 STM32 内部 I2C 外设控制 SCL/SDA
    起始位、停止位、ACK、时钟、数据收发主要由硬件完成

软件 I2C：
    使用普通 GPIO 手动模拟 SCL/SDA
    每一次拉高、拉低、读 ACK 都由代码控制
```

本项目当前 VL53L0X 使用的是软件 I2C，也就是代码中通过 GPIO 控制 PB10/PB11：

```text
SCL = PB10
SDA = PB11
addr = 0x29
```

## 观察

### 硬件 I2C 的特点

优点：

```text
1. 时序稳定，由硬件外设产生标准 I2C 波形
2. CPU 占用低，不需要软件一直翻转 GPIO
3. 可以配合中断或 DMA，提高效率
4. 更适合高速、长期稳定运行的产品代码
```

缺点：

```text
1. 必须使用芯片支持 I2C 复用功能的固定引脚
2. 某些 STM32 早期 I2C 外设在异常情况下可能出现 BUSY 卡死，需要恢复逻辑
3. 多个外设共用一条 I2C 总线时，需要统一规划地址和互斥访问
```

### 软件 I2C 的特点

优点：

```text
1. 引脚灵活，任意普通 GPIO 理论上都可以模拟 I2C
2. 调试直观，代码里可以控制每一个时钟和 ACK
3. 总线异常时可以手动拉 SCL/SDA 做 bus recovery
4. 适合教学、验证、引脚冲突时的补救方案
```

缺点：

```text
1. 时序依赖软件延时，延时太短可能导致从设备来不及响应
2. CPU 占用比硬件 I2C 高
3. FreeRTOS 多任务环境下需要注意互斥和临界区
4. 速度和稳定性通常不如硬件 I2C
```

本项目之前出现：

```text
VL53L0X FAIL ... status=addr-nack
```

其中一个原因就是软件 I2C 的 SCL/SDA 翻转太快，VL53L0X 有时还没稳定识别地址，STM32 就已经开始采样 ACK，最终表现为 `addr-nack`。后来把软件 I2C 延时从 `5us` 放慢到 `8us`，并增加 bus recovery 和多次探测后，日志恢复为：

```text
VL53L0X RANGE_OK ... model=0xEE dist=88mm filtered=1 err=0
```

## 结论

本项目使用软件 I2C 的主要原因是：

```text
1. 当前项目是学习和面试展示项目，软件 I2C 更容易观察底层时序和 ACK
2. 板子上已有多个外设，占用和复用引脚较多，软件 I2C 引脚选择更灵活
3. VL53L0X 调试过程中需要 bus recovery、地址探测、时序调整，软件 I2C 更方便验证
```

但如果做正式产品，或者系统对稳定性、效率、速率要求更高，应该优先使用硬件 I2C。

## 什么时候用软件 I2C

适合使用软件 I2C 的场景：

```text
1. 硬件 I2C 引脚已经被其他外设占用
2. 只是低速读取传感器，比如温湿度、距离、配置寄存器
3. 项目处于学习、验证、快速调试阶段
4. 需要手动恢复总线或观察 ACK 过程
5. 外设数量不多，通信频率不高
```

## 什么时候用硬件 I2C

适合使用硬件 I2C 的场景：

```text
1. 产品级项目，需要长期稳定运行
2. I2C 访问频率高，CPU 不能被软件翻转 GPIO 占用
3. 多个 I2C 设备共享总线，需要标准外设管理
4. 需要中断、DMA、低功耗唤醒等能力
5. 对时序一致性要求高
```

## 面试回答

问题：软件 I2C 和硬件 I2C 有什么区别？你这个项目为什么用软件 I2C？

30 秒回答：

软件 I2C 是用 GPIO 手动模拟 SCL 和 SDA，起始位、停止位、ACK、数据收发都由代码控制；硬件 I2C 是使用 STM32 内部 I2C 外设，由硬件产生标准时序。软件 I2C 的优点是引脚灵活、调试直观、方便做总线恢复，缺点是 CPU 占用更高，时序依赖软件延时，稳定性不如硬件 I2C。本项目是学习和面试展示项目，而且外设较多、引脚复用复杂，所以 VL53L0X 阶段先使用软件 I2C，方便定位 `addr-nack`、bus recovery 和初始化时序问题。如果做正式产品，我会优先选择硬件 I2C，并配合互斥锁、中断或 DMA 来提高稳定性和效率。

追问：为什么软件 I2C 延时会影响 `addr-nack`？

回答：

因为软件 I2C 的 SCL/SDA 翻转完全由代码延时决定。如果延时太短，SCL 高电平保持时间不够，从设备可能还没来得及采样地址或拉低 SDA 应答，主机就已经读取 ACK 位，最终会误判成 NACK。所以在这个项目里，我把延时从 `5us` 放慢到 `8us`，并在初始化前增加 bus recovery 和多次地址探测，让 VL53L0X 更稳定地 ACK。

# VL53L0X I2C online stage

## VL53L0X 调试复盘：从 ONLINE 到 RANGE_OK

### 最终成功现象

最终串口输出：

```text
VL53L0X RANGE_OK tick=160502 addr=0x29 model=0xEE dist=145mm reads=322 err=0
```

这说明 VL53L0X 已经不只是 I2C 在线，而是完成了真实测距：

- `addr=0x29`：I2C 7-bit 地址响应正常。
- `model=0xEE`：Model ID 读取正确。
- `RANGE_OK`：测距流程完成。
- `dist=145mm`：读到了有效距离。
- `err=0`：当前 VL53L0X 任务无错误累计。

### 问题 1：为什么一开始只有 ONLINE

问题：

一开始日志类似：

```text
VL53L0X ONLINE tick=502 addr=0x29 model=0xEE status=range-pending reads=2 err=0
```

操作：

把状态拆成两层：

- 设备在线：I2C 地址 `0x29` 有 ACK，Model ID 等于 `0xEE`。
- 数据有效：初始化、校准、启动测距、读取距离全部成功。

观察：

`ONLINE` 说明硬件连接和基础 I2C 读写正常，但 `range-pending` 说明测距流程还没有成功。

结论：

`ONLINE` 不等于 `RANGE_OK`。项目里必须区分“设备在线”和“数据有效”，否则会把硬件识别成功误判成业务功能成功。

### 问题 2：为什么曾经出现 model=0x00 或 OFFLINE

问题：

移植完整初始化流程后，曾经出现设备仍然 ACK，但 Model ID 读成 `0x00`，日志显示离线。

操作：

检查 VL53L0X 初始化序列，发现初始化过程中会切换内部寄存器页。于是增加默认页恢复：

```c
static void select_default_page(void)
{
    (void)BSP_I2C2_WriteByte(VL53L0X_ADDR, 0xFFU, 0x00U);
    (void)BSP_I2C2_WriteByte(VL53L0X_ADDR, 0x00U, 0x00U);
}
```

并在读取 Model ID 前调用。

观察：

修复后 Model ID 稳定恢复为：

```text
model=0xEE
```

结论：

这不是接线问题，而是寄存器页没有恢复导致读错地址空间。驱动移植时，失败退出路径也要恢复芯片状态。

### 问题 3：为什么卡在 spad-info-fail

问题：

日志出现：

```text
VL53L0X ONLINE tick=605502 addr=0x29 model=0xEE status=spad-info-fail reads=1212 err=0
```

操作：

继续细化 `range-pending` 的原因，并检查 `get_spad_info()`。原代码等待：

```c
0x83 == 0x01
```

后来改成：

```c
0x83 != 0x00
```

观察：

修改后错误从 `spad-info-fail` 推进到了 `ref-calibration-fail`。

结论：

SPAD 阶段已经通过。原来的状态位判断太严格，应该按“非 0 表示 ready”处理，而不是必须等于固定值。

### 问题 4：为什么卡在 ref-calibration-fail

问题：

SPAD 通过后，日志变成：

```text
VL53L0X ONLINE tick=246002 addr=0x29 model=0xEE status=ref-calibration-fail reads=493 err=0
```

操作：

检查参考校准流程，修正两处：

1. 第一段校准前先写：

```c
SYSTEM_SEQUENCE_CONFIG = 0x01
```

2. 等待中断完成从严格相等改成按位非 0：

```c
RESULT_INTERRUPT_STATUS & 0x07 != 0
```

观察：

修复后串口进入：

```text
VL53L0X RANGE_OK ... dist=145mm ... err=0
```

结论：

参考校准失败的根因是初始化顺序和状态位判断不准确。VL53L0X 的测距依赖内部校准流程，不能只靠基础 I2C 读写。

### 问题 5：为什么增加 BSP_I2C_WriteReg 连续写接口

问题：

VL53L0X 有 16 位寄存器写入需求，最初 `wr16()` 用两次单字节写实现。

操作：

在 BSP 软件 I2C 层增加连续写接口：

```c
BspI2cStatus BSP_I2C2_WriteReg(uint8_t addr7,
                               uint8_t reg,
                               const uint8_t *data,
                               size_t len);
```

然后让 `wr16()` 使用一次 I2C 事务连续写两个字节。

观察：

编译通过，MPU6050 和 VL53L0X 都能继续运行，VL53L0X 后续进入 `RANGE_OK`。

结论：

连续写更符合 I2C 寄存器访问语义，尤其适合 16 位寄存器和连续配置表。BSP 层提供通用接口，比驱动层拼多个单字节事务更稳。

### 为什么改了多次才成功

VL53L0X 比 DHT11 和 MPU6050 复杂：

- DHT11 主要是单总线时序。
- MPU6050 主要是 I2C 寄存器读写和 WHO_AM_I。
- VL53L0X 还需要寄存器页切换、SPAD 信息读取、tuning settings、参考校准、测距启动、中断等待和结果读取。

所以正确开发方法不是一次写完整驱动，而是分层验证：

```text
I2C scan 0x29
  -> Model ID 0xEE
  -> SPAD info
  -> reference calibration
  -> single ranging
  -> RANGE_OK dist=...mm
```

这次实际推进过程是：

```text
OFFLINE / model=0x00
  -> 修复寄存器页恢复
ONLINE status=range-pending
  -> 增加 LastStatus 细化状态
ONLINE status=spad-info-fail
  -> 修复 SPAD ready 判断条件
ONLINE status=ref-calibration-fail
  -> 修复校准顺序和中断完成判断
RANGE_OK dist=145mm
```

### 测试方法

正常测试：

1. 观察 I2C2 扫描是否包含：

```text
I2C2 scan: 0x29
```

2. 观察 Model ID 是否正确：

```text
VL53L0X init OK addr=0x29 model=0xEE
```

3. 观察是否最终进入：

```text
VL53L0X RANGE_OK ... dist=...mm ... err=0
```

分阶段测试：

- `model-fail`：检查 VCC、GND、SCL、SDA、模块供电。
- `spad-*`：检查 SPAD 获取序列和寄存器页恢复。
- `cal-*`：检查参考校准顺序和中断状态位判断。
- `range-*`：检查单次测距启动、等待完成和结果寄存器读取。

并发稳定性测试：

- VL53L0X 每 500ms 上报一次。
- MPU6050 每 100ms 上报一次。
- DHT11 每 2000ms 上报一次。
- Heartbeat 每 1000ms 输出一次。

观察这些日志是否都能持续输出，heap 是否稳定，确认 VL53L0X 不会阻塞整个 FreeRTOS 系统。

故障测试：

- 拔掉 VL53L0X SDA 或 VCC，预期 `I2C2 scan` 找不到 `0x29`，VL53L0X 输出 `FAIL`。
- 恢复接线后，任务应能重新初始化并回到 `ONLINE` 或 `RANGE_OK`。

### 面试回答版本

问题：你在 VL53L0X 开发中遇到了什么问题？怎么解决？

30 秒回答：

我遇到的主要问题是 VL53L0X 一开始只能在线识别，不能输出有效距离。我没有直接盲改驱动，而是把状态拆成 `ONLINE`、`range-pending`、`spad-info-fail`、`ref-calibration-fail`、`RANGE_OK`。通过串口日志逐步定位到三个问题：寄存器页切换后没有恢复、SPAD ready 判断条件过严、参考校准顺序和中断完成判断不准确。逐项修正后，最终串口稳定输出 `RANGE_OK dist=145mm err=0`。这个过程体现的是驱动移植中的分层验证和可观测性设计。

深入解释：

VL53L0X 不是简单 I2C 传感器，它内部测距前需要执行复杂初始化。我按 I2C scan、Model ID、SPAD、tuning、calibration、range start 的顺序逐步验证，并且每一步都加状态输出。这样可以证明每次修改是否真的让流程向后推进，也能避免把硬件问题和驱动流程问题混在一起。

追问：

- 为什么 `ONLINE` 不等于 `RANGE_OK`？
- 为什么 VL53L0X 需要 SPAD 配置？
- 为什么等待状态位不能无限阻塞？
- 为什么驱动层要保留超时？
- 为什么 I2C 需要连续写接口？

## 技术原理

VL53L0X 是 I2C 激光测距模块，默认 7-bit 地址为 `0x29`。本阶段先完成在线识别：

- I2C2 SCL: PB10
- I2C2 SDA: PB11
- I2C 地址: `0x29`
- Model ID 寄存器: `0xC0`
- 期望 Model ID: `0xEE`

注意：本阶段验证的是设备在线和寄存器可读，不等于完整测距功能已经完成。

## 为什么先做在线识别

VL53L0X 的完整测距初始化流程比 MPU6050 复杂，需要 SPAD、校准、时序预算等配置。
如果直接做完整测距，问题可能来自接线、I2C 地址、寄存器读写、初始化序列或测距流程。

所以本阶段先分层验证：

1. I2C2 scan 能否发现 `0x29`。
2. Model ID 是否能读到 `0xEE`。
3. 通过 Queue 上报 `ONLINE` 或 `FAIL`。

这样可以清楚区分“硬件在线”和“测距完成”。

## 技术方案比较

### 方案 A：直接移植完整 VL53L0X 测距驱动

优点：

- 一步到位，能直接输出距离。

缺点：

- 初始化复杂，调试难度高。
- 失败时不容易判断是硬件问题还是算法/寄存器序列问题。

### 方案 B：先做 I2C 在线识别，再做测距

优点：

- 可验证粒度小。
- 先确认地址和 Model ID，硬件问题容易定位。
- 更适合面试讲调试分层。

缺点：

- 第一阶段还不能输出真实距离。

当前选择：

本阶段采用方案 B，只输出 `ONLINE`、Model ID 和 `range-pending` 状态。

## 当前数据流

```text
TaskVL53L0X
  -> xSemaphoreTake(s_i2c2_mutex)
  -> BSP_I2C2_IsReady(0x29)
  -> VL53L0X_ReadModelId()
  -> xSemaphoreGive(s_i2c2_mutex)
  -> xQueueSend(AppMessage)
  -> TaskSensorLog
  -> UART
```

## 高频面试题

### 问题：为什么日志显示 ONLINE 但不是 RANGE_OK？

30 秒回答：

因为当前阶段只验证了 VL53L0X 的 I2C 在线和 Model ID，可证明硬件连接、地址 ACK
和寄存器读取链路正常。但 VL53L0X 完整测距还需要额外初始化、校准和测距启动流程，
所以我明确把状态标成 `online-range-pending`，不把在线识别误认为完整测距。

深入解释：

I2C scan 只能证明地址有 ACK，Model ID 能证明设备身份和寄存器读取正常。但距离值
需要经过传感器内部测距流程，涉及更多寄存器配置和中断/状态位判断。工程上应该
分层验证，避免把不同问题混在一起。

项目结合：

正常在线日志预期：

```text
I2C2 scan: 0x29
VL53L0X init OK addr=0x29 model=0xEE
VL53L0X ONLINE tick=... addr=0x29 model=0xEE status=online-range-pending reads=... err=0
```

追问：

- I2C scan 到 `0x29` 是否等于测距成功？
- Model ID 有什么作用？
- 如果 scan 有 `0x29` 但 Model ID 不是 `0xEE`，你怎么判断？
- 完整测距下一步要补哪些流程？

### 问题：从 ONLINE 到 RANGE_OK 需要补什么？

30 秒回答：

`ONLINE` 只证明 I2C 地址和 Model ID 正常；`RANGE_OK` 需要完成 VL53L0X 的测距
初始化和一次测距流程。当前代码在在线识别基础上增加了 SPAD 信息读取、tuning
settings、参考校准和单次测距启动，然后根据状态位读取距离。如果测距成功就输出
`RANGE_OK dist=...mm`，否则保持 `RANGE_PENDING`，不把未完成的测距误报为成功。

深入解释：

VL53L0X 距离读取不是简单读一个固定寄存器。它需要先完成设备内部初始化，包括：

1. 设置 I2C 电平兼容配置。
2. 读取 stop variable。
3. 配置 signal rate limit。
4. 获取并配置 reference SPAD。
5. 加载 tuning settings。
6. 执行 reference calibration。
7. 启动单次测距。
8. 等待 interrupt/status 位。
9. 读取 range 结果并清除 interrupt。

项目结合：

当前日志有三种状态：

```text
VL53L0X FAIL ...
VL53L0X ONLINE ... status=range-pending
VL53L0X RANGE_OK ... dist=123mm
```

这样可以清楚地区分：

- `FAIL`: 设备不在线或寄存器不可读。
- `ONLINE/range-pending`: 设备在线，但测距结果暂未有效。
- `RANGE_OK`: 已经读到有效距离。

追问：

- 为什么不能把 Model ID 正确等同于测距成功？
- 如果一直 RANGE_PENDING，你会怎么定位？
- VL53L0X 的测距状态位有什么作用？
- 为什么驱动里所有等待都要有超时？

## 测试设计

正常测试：

- VL53L0X VCC/GND 正确连接。
- PB10 接 SCL，PB11 接 SDA。
- 串口观察 I2C2 scan 是否有 `0x29`。
- 观察 Model ID 是否为 `0xEE`。

边界测试：

- 长时间运行 5 分钟，观察 heap 是否稳定。
- 同时观察 DHT11、MPU6050、Heartbeat 是否仍然正常。

异常测试：

- 拔掉 VL53L0X SDA，观察 `VL53L0X FAIL`，DHT11 和 MPU6050 是否继续运行。
- 拔掉 VL53L0X VCC，观察 I2C2 scan 是否不再出现 `0x29`。

性能测试：

- 观察 VL53L0X online 日志 tick 间隔是否约为 500ms。
- 观察 MPU6050 100ms 周期是否仍然稳定。
