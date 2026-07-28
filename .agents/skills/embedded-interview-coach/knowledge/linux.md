# Linux面试知识

## TCP粘包

Q:
为什么TCP会粘包？

A:
TCP提供的是连续字节流，没有消息边界。

应用层需要自己定义协议。


## epoll

Q:
为什么使用epoll？

A:
select/poll需要遍历所有fd。

epoll通过事件通知减少无效扫描，适合大量连接。
