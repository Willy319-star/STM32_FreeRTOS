# Linux 设备树与驱动适配清单

## 硬件确认

修改软件前先确认：

- 总线类型和控制器编号
- I²C 地址 / SPI CS
- 电源与电压
- 时钟
- 复位 GPIO
- 中断 GPIO、触发极性
- pinmux
- 物理波形

## 设备树核心概念

必须能解释：

- DTS、DTSI、DTB
- 节点名和 unit-address
- `compatible`
- `reg`
- `status`
- `pinctrl`
- `*-gpios`
- `interrupt-parent` 与 `interrupts`
- clocks、resets、regulators
- phandle

## 运行时验证

不要只看源码 DTS，检查运行时设备树：

- `/proc/device-tree`
- `/sys/firmware/devicetree/base`
- 反编译当前 DTB
- 确认系统是否加载了新 DTB

## 匹配流程

设备树节点
-> 总线/平台设备创建
-> `of_match_table`
-> `compatible` 匹配
-> `probe`
-> 获取资源
-> 初始化硬件
-> 注册子系统/设备节点

## 驱动适配和驱动开发的边界

- 只修改 DTS、配置和小范围兼容代码：称为“驱动适配”。
- 实现完整总线交互、子系统注册和用户接口：可称为“驱动开发”。
- 面试中必须准确说明自己的工作边界。

## 模块构建

掌握：

- Kconfig/Makefile
- 内建与模块
- `.ko`
- `insmod/modprobe/rmmod`
- `lsmod/modinfo`
- vermagic
- `dmesg`

## probe 审查

- 每个资源获取是否检查错误
- `-EPROBE_DEFER` 是否正确传播
- 芯片 ID 是否验证
- 中断是否可能风暴
- 错误路径是否释放资源
- `remove` 是否停止硬件
- 是否适合使用 `devm_*`

## 故障注入

- compatible 写错
- reg/I²C 地址写错
- pinctrl 写错
- GPIO 极性写错
- IRQ 触发类型写错
- regulator/clock 缺失
- 模块内核版本不匹配
- 驱动加载但用户节点未出现

## 面试追问

- probe 为什么不执行？
- `status = "okay"` 的作用是什么？
- `reg` 在 I²C 和 platform memory 中分别表示什么？
- `modprobe` 与 `insmod` 的差异是什么？
- 为什么出现 probe defer？
- 设备节点未生成应如何分层排查？
