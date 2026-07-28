# EventGroup alert stage

## 阶段目标

问题：
传感器数据已经能采集、打印、协议发送，下一步系统还能做什么？

操作：
新增 `app_control.c/.h`，创建 `ControlTask` 和一个 FreeRTOS `EventGroup`。传感器汇总任务 `TaskSensorLog` 每收到一条 `AppMessage`，调用：

```c
AppControl_UpdateFromSensor(&msg);
```

控制模块根据传感器值更新告警位。

观察：
串口会周期性输出类似：

```text
ControlTask start: EventGroup alert monitor
ALERT bits=0x0004 hot=0 dry=0 near=1 pot_hi=0 motion=0 fail=0x00
```

结论：
项目从“数据采集节点”升级为“有状态判断能力的实时节点”。这一步体现的是 FreeRTOS 任务间状态同步和业务解耦。

## 当前告警规则

| 告警 | 条件 |
| --- | --- |
| `hot` | DHT11 温度 `>= 32C` |
| `dry` | DHT11 湿度 `<= 30%` |
| `near` | VL53L0X 距离有效且 `< 120mm` |
| `pot_hi` | 电位器百分比 `>= 80%` |
| `motion` | MPU6050 任意轴角速度绝对值 `> 100dps` |
| `fail` | 任一传感器当前上报 invalid |

这些阈值不是传感器的物理极限，而是项目演示阈值。目的是证明系统可以基于多传感器数据做实时状态判断。

## 为什么使用 EventGroup

问题：
为什么这里不用 Queue，而是用 EventGroup？

操作：
把每一种告警状态映射成一个 bit：

```text
bit0 hot
bit1 dry
bit2 near
bit3 pot_hi
bit4 motion
bit8 DHT11 fail
bit9 MPU fail
bit10 VL53 fail
bit11 POT fail
```

观察：
这些状态不是一条条历史数据，而是“当前系统状态”。例如 `near=1` 表示当前距离太近，`near=0` 表示恢复正常。多个状态可以同时存在，比如又近又干燥。

结论：
Queue 适合传递一条条数据；EventGroup 适合表达多个二值状态的组合。这里告警状态天然适合用 bit 表示，所以 EventGroup 比 Queue 更直接。

## Queue 和 EventGroup 的区别

问题：
Queue 和 EventGroup 都能做任务间通信，它们区别是什么？

操作：

- Queue：传递数据内容，有顺序，有缓存深度。
- EventGroup：传递状态位，不关心历史，只关心当前哪些 bit 被置位。

观察：
传感器采样数据需要 Queue，因为每条数据都有温度、距离、时间戳等内容；告警状态用 EventGroup，因为它只需要表达“是否高温、是否距离过近、是否故障”。

结论：
这个项目里两者同时使用：

```text
SensorTask -> Queue -> TaskSensorLog
TaskSensorLog -> EventGroup -> ControlTask
```

这说明任务间通信不是固定只用一种机制，而是根据数据语义选择。

## 为什么不在传感器任务里直接判断告警

问题：
每个传感器任务采到数据后直接判断告警不行吗？

操作：
当前设计让传感器任务只负责采集；`TaskSensorLog` 作为汇总点，把消息交给 `AppControl_UpdateFromSensor()`。

观察：
如果每个传感器任务都直接处理告警，业务逻辑会分散在多个任务里。以后修改阈值、增加组合条件、增加报警输出，就要改很多地方。

结论：
把采集和业务决策分开，系统更容易维护。传感器任务是数据源，控制模块是业务规则，这是一种典型的分层设计。

## 面试回答：你项目里 EventGroup 用来做什么

30 秒回答：
我在项目里用 EventGroup 表达系统告警状态。传感器数据先通过 Queue 汇总到 `TaskSensorLog`，然后控制模块根据温度、湿度、距离、电位器和 MPU6050 数据更新 EventGroup 的 bit。比如 bit0 表示高温，bit2 表示距离过近，bit8 到 bit11 表示不同传感器故障。ControlTask 周期读取这些 bit 并打印当前状态。Queue 用来传递具体采样数据，EventGroup 用来表达多个当前状态，这样语义比较清晰。

## 面试追问：EventGroup 适合传递传感器数据吗

回答：
不适合传递完整传感器数据。EventGroup 只有 bit 状态，适合表达“发生/未发生”的事件或状态，比如告警、连接状态、故障标志。如果要传递温度值、距离值、时间戳，就应该用 Queue 或消息缓冲区。这个项目里我用 Queue 传递 `AppMessage`，用 EventGroup 表达告警状态，两者分工不同。
