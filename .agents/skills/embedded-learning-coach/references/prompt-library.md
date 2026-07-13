# Codex 使用提示词库

## 学习概念

```text
$embedded-learning-coach

我想通过实践理解 FreeRTOS Mutex 和优先级反转。
使用 LEARN 模式。不要直接给完整答案：
1. 设计一个 STM32 上可观察的三任务实验；
2. 让我先预测波形；
3. 告诉我如何用 GPIO 和逻辑分析仪测量；
4. 最后再根据结果解释。
```

## 建设里程碑

```text
$embedded-learning-coach

使用 BUILD 模式。阅读当前仓库和 AGENTS.md。
把 UART DMA + IDLE + 环形缓冲区拆成最多 5 个可独立验收的里程碑。
先给接口、数据所有权、时序和测试，不要生成完整实现。
```

## 调试

```text
$embedded-learning-coach

使用 DEBUG 模式。probe 没有执行。
请基于当前 DTS、of_match_table 和 dmesg：
- 区分事实与假设；
- 给出不超过 5 个原因；
- 选择信息增益最高的下一条命令；
- 不要先修改代码。
```

## 审查协议代码

```text
$embedded-learning-coach

使用 REVIEW 模式，审查已打开的 protocol.c。
重点检查半包、连续帧、噪声重同步、长度溢出、CRC、大小端和线程安全。
不要直接修改代码，每个问题给最小复现字节序列。
```

## 模拟面试

```text
$embedded-learning-coach

使用 INTERVIEW 模式，根据当前仓库模拟大疆/华为嵌入式软件面试。
从我的 FreeRTOS 采集节点开始，一次只问一个问题。
逐层追问使用、原理、实现、故障和取舍。
```

## 生成简历证据

```text
$embedded-learning-coach

使用 RESUME 模式，扫描当前项目。
把能力分成 VERIFIED/PARTIAL/PLANNED/UNSUPPORTED。
只有 VERIFIED 生成简历 bullet，并列出证据文件和可能追问。
```

## 分析日志

```text
$embedded-learning-coach

使用 DEBUG 模式。下面是 dmesg、strace 和应用日志。
不要改代码，先列：
1. 已确认事实；
2. 排名后的假设；
3. 每个假设的证据；
4. 下一步实验及不同输出的含义。
```
