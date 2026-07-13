# Linux 系统编程、网络与调试清单

## 进程与线程

必须理解并实践：

- `fork` 返回值和地址空间写时复制
- `exec` 替换进程映像
- `wait/waitpid` 回收子进程
- 僵尸进程与孤儿进程
- `SIGCHLD`、`SIGTERM` 和优雅退出
- 进程与线程的隔离、通信成本和故障范围

## IPC

比较并至少实现两种：

- Pipe/FIFO
- Unix Domain Socket
- POSIX/System V 消息队列
- 共享内存 + 同步原语

每种方案说明：

- 消息边界
- 复制次数
- 阻塞行为
- 所有权和清理
- 生产者/消费者速度不匹配
- 进程崩溃后的资源状态

## 文件描述符和 I/O

检查：

- 所有系统调用返回值
- `errno` 上下文
- `EINTR` 和 `EAGAIN`
- 部分读写
- 阻塞/非阻塞
- fd 泄漏
- `select/poll/epoll`
- LT/ET 行为

## TCP

必须处理：

- 应用层消息边界
- 半包和连续帧
- `recv == 0`
- 部分 `send`
- 慢客户端和背压
- 心跳、超时和重连
- `TIME_WAIT`
- 优雅关闭与异常关闭

## UDP

必须理解：

- 不保证到达、顺序和唯一性
- 序列号、丢包和乱序统计
- MTU 和分片
- 适合低时延数据的条件
- 可靠性需要由应用层补充

## Shell 与服务管理

建议具备：

- 参数、变量、判断、循环、函数
- `set -euo pipefail`
- 管道与重定向
- `grep/sed/awk/find/xargs`
- 自动编译、部署、测试、日志收集
- systemd 启动、停止、重启和日志

## 调试工具

- `ps/top/pidstat/perf`
- `free/vmstat/pmap`
- `strace/lsof/gdb/core`
- `ip/ss/ping/tcpdump/iperf`
- `dmesg/journalctl`
- `/proc/<pid>/`

## 故障训练

主动制造：

- 忙循环导致 CPU 高
- 内存泄漏
- fd 泄漏
- 子进程未回收
- 串口权限错误
- 网络端口占用
- 客户端断线
- 慢客户端
- 配置文件缺失
- OOM Killer

## 面试追问

- 共享内存为什么快，为什么仍需同步？
- `recv` 返回 0 是什么？
- epoll ET 模式为什么必须读到 EAGAIN？
- 如何处理 `send` 的部分写？
- 如何从网络不通逐层排查？
- 如何让服务优雅退出并确保子进程回收？
