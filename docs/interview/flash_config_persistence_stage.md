# Flash 参数掉电保存阶段

## 这一阶段做了什么

上一阶段的 `SET NEAR 80` 只改 RAM，断电后会恢复默认值。

本阶段新增内部 Flash 保存能力：

```text
CONFIG
SET NEAR 80
SAVE CONFIG
LOAD CONFIG
DEFAULT CONFIG
```

注意：这些命令都在 XCOM 串口助手中发送。

## 问题

为什么运行时配置还需要保存到 Flash？

## 操作

运行时阈值保存在 RAM 里的 `s_config`。

RAM 的特点是：

```text
运行时读写方便
断电后数据丢失
```

Flash 的特点是：

```text
断电后不丢失
写入前必须按扇区擦除
擦写次数有限
写入速度比 RAM 慢
```

所以本项目采用：

```text
平时使用 RAM 配置
用户确认后，发送 SAVE CONFIG 保存到 Flash
启动时从 Flash 读取配置
```

## 观察

启动时会打印：

```text
CONFIG load: flash valid
```

或者：

```text
CONFIG load: flash empty/invalid, defaults active
```

如果 Flash 中没有有效配置，就使用默认阈值。

## 结论

Flash 保存解决的是“参数掉电丢失”的问题。

RAM 配置适合快速调试，Flash 配置适合保存最终参数。

## Flash 区域选择

### 问题

配置应该保存到 Flash 的哪里？

### 操作

STM32F407VET6 当前工程 Flash 总大小按 512KB 使用。

本项目约定：

```text
应用程序区：0x08000000 ~ 0x0805FFFF
配置参数区：0x08060000 ~ 0x0807FFFF
```

也就是把最后一个 128KB 扇区，也就是 sector 7，留给配置参数。

链接脚本中把程序可用 Flash 从 512KB 缩小到 384KB：

```ld
FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 384K
```

这样可以避免以后程序变大后覆盖配置区。

### 观察

当前程序大小远小于 384KB，所以保留 sector 7 不影响运行。

### 结论

Flash 参数区必须和程序区隔离。

如果不隔离，将来程序变大后可能会把保存的配置覆盖掉。

## 数据格式设计

### 问题

为什么保存配置时不能只把几个阈值直接写进去？

### 操作

Flash 中保存的是一条带校验的记录：

```text
magic
version
config
crc
```

含义：

```text
magic   用来判断这块 Flash 是否真的是本项目配置
version 用来支持以后配置结构升级
config  真正的告警阈值
crc     用来判断数据是否损坏
```

### 观察

启动读取 Flash 时，如果出现：

```text
magic 不匹配
version 不匹配
阈值范围非法
crc 校验失败
```

系统都会认为 Flash 配置无效，然后回退到默认配置。

### 结论

Flash 参数保存不能只考虑“写进去”，还要考虑“怎么判断读出来的数据可信”。

这就是 `magic + version + crc` 的意义。

## 命令流程

### 问题

`SAVE CONFIG` 具体做了什么？

### 操作

流程如下：

```text
XCOM 发送 SAVE CONFIG
        ↓
CommandTask 解析命令
        ↓
读取当前 AppControlConfig
        ↓
生成 Flash record
        ↓
计算 CRC
        ↓
解锁 Flash
        ↓
擦除 sector 7
        ↓
按 word 写入记录
        ↓
锁定 Flash
        ↓
重新读取并校验
```

### 观察

成功时会看到：

```text
CMD SAVE CONFIG: erasing Flash sector 7...
CMD SAVE CONFIG OK
```

### 结论

保存配置是一个慢操作，并且会擦除整个 sector。

所以项目没有在每次 `SET` 后自动保存，而是要求用户明确发送 `SAVE CONFIG`。

## 为什么不每次 SET 自动保存

### 问题

为什么 `SET NEAR 80` 后不自动写 Flash？

### 操作

Flash 写入前要擦除，而且擦除以扇区为单位。

如果每次旋钮变化或每次调参都写 Flash，会导致：

```text
Flash 擦写次数增加
系统短时间卡顿
参数区寿命下降
```

### 观察

当前设计中：

```text
SET xxx       只改 RAM
SAVE CONFIG   用户确认后才写 Flash
```

### 结论

这是一个可靠性设计：频繁变化的数据放 RAM，确认后的长期参数才写 Flash。

## 面试回答

问题：你的项目配置参数如何做到掉电保存？

30秒回答：

我把运行时阈值先保存在 RAM 中，用户可以通过 XCOM 命令实时修改。需要掉电保存时，发送 `SAVE CONFIG`，系统会把当前配置封装成带 `magic/version/crc` 的记录，擦除 STM32F407 的 Flash sector 7，然后按 word 写入。启动时系统会从 sector 7 读取配置并校验，如果校验通过就使用 Flash 参数，如果无效就回退默认值。

深入解释：

我没有让每次 `SET` 都自动写 Flash，因为 Flash 擦写以扇区为单位，写入速度慢，并且有寿命限制。当前方案把“调试过程”和“确认保存”分开，RAM 用于快速调参，Flash 用于长期保存。链接脚本也把应用程序 Flash 限制到 384KB，把 sector 7 保留给参数区，避免程序和配置互相覆盖。

项目结合：

例如我可以发送 `SET NEAR 80` 调整 VL53L0X 近距离告警阈值，观察 `ALERT near` 是否符合预期。确认这个阈值合适后，再发送 `SAVE CONFIG` 保存到 Flash。之后断电重启，启动日志如果显示 `CONFIG load: flash valid`，说明参数恢复成功。

追问：

为什么要有 CRC？

回答：

因为 Flash 数据可能因为写入中断电、擦写异常或版本变化而不可信。CRC 可以检查整条记录是否被破坏。如果 CRC 不匹配，系统不能继续使用这份配置，而是回退默认值，避免错误参数影响告警判断。

## 测试清单

正常测试：

```text
CONFIG
SET NEAR 80
SAVE CONFIG
复位或断电重启
CONFIG
```

预期重启后仍然看到：

```text
near=80mm
```

边界测试：

```text
SET NEAR 10
SAVE CONFIG
SET NEAR 2000
SAVE CONFIG
```

异常测试：

```text
SET NEAR 0
SET POT 101
SET MOTION 0
```

预期不会写入非法配置。

可靠性观察：

```text
保存配置后继续观察 HEARTBEAT、HEALTH OK、传感器日志。
确认系统没有因为 Flash 写入长期阻塞。
```
