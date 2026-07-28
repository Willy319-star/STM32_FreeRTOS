# Linux Driver面试知识

## device tree

Q:
为什么需要设备树？

A:
为了将硬件描述和驱动代码分离。


## compatible

Q:
compatible有什么作用？

A:
设备树通过compatible匹配驱动中的of_match_table。


## probe

Q:
probe什么时候执行？

A:
设备和驱动匹配成功后，内核调用probe完成初始化。
