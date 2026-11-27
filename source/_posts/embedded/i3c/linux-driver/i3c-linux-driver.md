---
title: Linux I3C 驱动视角学习笔记
date: 2026-11-25
permalink: 2026/11/25/i3c-linux-driver/
categories: ["嵌入式开发","I3C","Linux 驱动"]
tags: ["i3c","linux","driver","ibi","tutorial"]
toc: true
max_depth: 3
mermaid: true
description: 把 I3C 协议动作对应到 Linux core、controller driver 和 device driver，梳理设备模型与常用 API。
---

> 这份笔记站在 Linux 内核驱动的角度重新理解 I3C。  
> 它不是协议综述，而是回答几个更工程化的问题：
>
> - 协议里的 `Master / Slave / DAA / CCC / IBI`，在 Linux 里分别落到哪些对象和 API 上？
> - 普通 I3C 设备驱动到底该关心什么，不该关心什么？
> - I3C 控制器驱动和普通 I3C 设备驱动，边界在哪里？
> - I2C/I3C 混合总线在 Linux 子系统里是怎么建模的？
>
> 这份笔记参考了本机 Linux 6.8 头文件中的 I3C 定义，主要来自：
> `/usr/src/linux-hwe-6.8-headers-6.8.0-106/include/linux/i3c/device.h`、  
> `/usr/src/linux-hwe-6.8-headers-6.8.0-106/include/linux/i3c/master.h`、  
> `/usr/src/linux-hwe-6.8-headers-6.8.0-106/include/linux/regmap.h`。

<!-- more -->

## 1. 先换一个视角：协议世界和 Linux 世界不是一一平铺的

协议文档的讲法通常是：

- 总线上有 Main Master、Current Master、Slave
- 总线上发生 SDR、DAA、IBI、Hot-Join、CCC 等事务

Linux 内核的讲法则不同。它不会直接按“协议章节”组织代码，而是按职责拆层：

- `controller driver`：真正驱动 I3C 控制器硬件
- `I3C core`：管理总线、设备枚举、地址分配后的设备建模、驱动绑定
- `device driver`：具体某个 I3C 外设的功能驱动

所以学习 Linux I3C 时，必须先接受一个事实：

> 协议里的一个动作，到了 Linux 里，往往要被 core、controller driver、device driver 三方分摊。

举个最典型的例子：

- 协议上说“做一次 DAA”
- Linux 里却意味着：
  - core 维护地址槽和总线设备列表
  - controller driver 真正往硬件上发 `ENTDAA`
  - core 再把发现到的设备包装成 `i3c_device`
  - 最终普通设备驱动才收到 `probe()`

这就是为什么“懂协议”还不够，必须再补一层“Linux 怎么落地”。

## 2. Linux I3C 子系统的三层分工

我会把 Linux I3C 子系统记成下面这三层。

### 2.1 最底层：I3C 控制器驱动

这一层的对象是：

- `struct i3c_master_controller`
- `struct i3c_master_controller_ops`

这层驱动面对的是控制器硬件本身，负责：

- 初始化总线
- 发送 `CCC`
- 发起 `DAA`
- 执行私有 SDR 传输
- 执行兼容 I2C 传输
- 如果硬件支持，还要处理 `IBI`、`Hot-Join`、主控权交接等能力

> controller driver 负责“怎么把协议动作变成硬件事务”。

### 2.2 中间层：I3C core

这一层是 Linux 子系统真正的“组织者”。它负责：

- 建模总线对象
- 维护地址槽位
- 跟踪当前 master
- 维护 I3C 设备和 I2C 设备列表
- 在设备出现后创建 Linux 设备对象
- 绑定普通设备驱动
- 帮普通设备驱动提供统一 API

> core 负责“总线秩序”和“把硬件发现结果变成 Linux 设备模型”。

### 2.3 最上层：普通 I3C 设备驱动

这一层就是我们平时最常写的功能驱动，面对的是：

- `struct i3c_device`
- `struct i3c_driver`

它通常只需要关心：

- 自己是不是匹配到目标器件
- 设备的 `PID / BCR / DCR / dyn_addr` 等基础信息
- 怎么做 private transfer
- 是否需要启用 `IBI`
- 是否用 `regmap`

> 普通设备驱动负责“这个传感器/芯片的功能”，不负责“总线怎么建起来”。

## 3. 先记住几个最关键的内核对象

这一节是整篇最重要的对象地图。

### 3.1 `struct i3c_master_controller`

这是控制器驱动的核心对象。  
从本机 6.8 头文件看，它内部至少包含这些关键成员：

- `struct device dev`
- `struct i3c_dev_desc *this`
- `struct i2c_adapter i2c`
- `const struct i3c_master_controller_ops *ops`
- `struct i3c_bus bus`
- `struct workqueue_struct *wq`

这里最值得注意的点有两个：

- 它里面直接带了一个 `struct i2c_adapter`，说明 Linux 把 I3C 控制器同时看成“也能托管 legacy I2C 设备”的一类控制器。
- 它里面直接带了 `struct i3c_bus`，说明总线状态本身是内核要显式维护的对象，不只是控制器的隐含状态。

### 3.2 `struct i3c_bus`

这是总线对象。它最关键的成员包括：

- `cur_master`
- `addrslots`
- `mode`
- `scl_rate.i3c`
- `scl_rate.i2c`
- `devs.i3c`
- `devs.i2c`
- `lock`

这个结构非常说明问题，因为它揭示了 Linux 对 I3C 的核心认知：

- 这是一条总线，不只是一个控制器
- 总线可以多主
- 总线上既有 I3C 设备，也可能有 I2C 设备
- 地址槽位是 core 显式维护的

其中 `addrslots` 很关键，因为它直接对应协议里的地址管理和 DAA。

还有一个非常容易误解的点：  
`i3c_bus.lock` 主要保护的是 **bus 级别状态变化**，比如地址回收、设备重枚举、总线维护动作。  
它**不等于**“普通设备驱动的所有传输天然会被这个锁自动串行化”。

换句话说：

> 多个设备驱动可以并发发起请求，但 controller driver 仍然必须保证底层帧不会互相交错。

### 3.3 `struct i3c_dev_desc`

这是 **core 和 controller driver** 更常接触到的“内部设备描述符”。  
它不是普通设备驱动直接操作的对象。

它里面的重要成员有：

- `struct i3c_device_info info`
- `struct i3c_device_ibi_info *ibi`
- `struct i3c_device *dev`
- `const struct i3c_dev_boardinfo *boardinfo`

我对它的理解是：

> `i3c_dev_desc` 是“内核内部对一个 I3C 设备的低层描述”。  
> controller driver 基本围着它转，普通设备驱动则主要看不到它。

### 3.4 `struct i3c_device`

这是 **普通 I3C 设备驱动** 面对的对象。  
头文件里能看到它至少包含：

- `struct device dev`
- `struct i3c_dev_desc *desc`
- `struct i3c_bus *bus`

注意这个关系：

- `i3c_dev_desc` 是内部描述
- `i3c_device` 是暴露给普通驱动的设备对象

所以我会这样记：

> controller driver 面向 `desc`，普通设备驱动面向 `device`。

### 3.5 `struct i3c_device_info`

这个结构基本就是普通设备驱动最喜欢看的“器件摘要信息”。

按本机头文件，它包含：

- `pid`
- `bcr`
- `dcr`
- `static_addr`
- `dyn_addr`
- `hdr_cap`
- `max_ibi_len`
- `max_read_len`
- `max_write_len`
- 以及若干速率/turn-around 相关字段

这个结构的一个关键注释是：

> 对于通过 `i3c_master_add_i3c_dev_locked()` 挂上的 I3C slave，core 会发送相关 `CCC` 把这些基础信息取回来。

这句话非常重要，因为它告诉我们：

- `PID / BCR / DCR` 这类基础能力信息，不一定要普通设备驱动自己去发 `CCC` 取
- 很多时候 core 在设备建模时就已经帮你拿到了

## 4. 普通 I3C 设备驱动，真正应该关心什么

这部分是最实用的。

### 4.1 最基本的驱动模型：`struct i3c_driver`

从 `include/linux/i3c/device.h` 来看，普通 I3C 驱动模型很直接：

```c
struct i3c_driver {
	struct device_driver driver;
	int (*probe)(struct i3c_device *dev);
	void (*remove)(struct i3c_device *dev);
	const struct i3c_device_id *id_table;
};
```

也就是说，普通 I3C 驱动本质上就是：

- 一张匹配表
- 一个 `probe()`
- 一个 `remove()`

### 4.2 匹配怎么做

常用方式是通过 `i3c_device_id` 表匹配，头文件里提供了几个宏：

- `I3C_DEVICE(...)`
- `I3C_DEVICE_EXTRA_INFO(...)`
- `I3C_CLASS(...)`

我对这几种匹配的理解是：

- 设备已经能明确到厂商和料号，就按 `PID` 细粒度匹配
- 如果更关心类别，就可以按 `DCR` 做类匹配

### 4.3 `probe()` 里第一件事通常是什么

通常不是立刻发传输，而是先把设备基础信息读出来。

Linux 提供了：

```c
void i3c_device_get_info(const struct i3c_device *dev,
			 struct i3c_device_info *info);
```

这一步非常有用，因为你能直接拿到：

- `pid`
- `bcr`
- `dcr`
- `dyn_addr`
- `max_ibi_len`

这样驱动可以先判断：

- 当前绑定到的是不是预期器件
- 设备是否支持 IBI
- 设备的数据长度、速率能力大概怎样

### 4.4 私有数据怎么挂

普通 I3C 驱动同样可以用常规设备模型那套私有数据挂接方式：

- `i3cdev_set_drvdata()`
- `i3cdev_get_drvdata()`

所以从驱动写法上说，I3C 设备驱动和 SPI/I2C 驱动在“probe 挂私有结构体”这件事上差异并不大。

### 4.5 真正做数据收发时用什么

普通 I3C SDR 私有传输的标准入口是：

```c
int i3c_device_do_priv_xfers(struct i3c_device *dev,
			     struct i3c_priv_xfer *xfers,
			     int nxfers);
```

这里的 `struct i3c_priv_xfer` 就对应一段私有 SDR 传输。

这一点很值得明确：

> 普通 I3C 设备驱动最常用的数据面入口，是 `i3c_device_do_priv_xfers()`；  
> 不是自己去拼底层 `CCC` 或手写地址仲裁。

### 4.6 普通设备驱动通常不该亲自干什么

普通设备驱动通常 **不该** 直接负责下面这些动作：

- 跑 DAA
- 管理地址槽位
- 决定总线模式
- 直接控制 Hot-Join 开关
- 管整个 bus 级别的 `CCC` 策略

这些更接近：

- I3C core
- controller driver
- 或系统初始化流程

这条边界非常重要。  
很多设计上的混乱，都是因为把“设备功能驱动”和“总线管理驱动”揉到一起了。

## 5. 一个普通 I3C 设备驱动的脑内模板

如果我自己写一个 I3C 传感器驱动，我脑中的最小模板会是这样：

```c
struct my_i3c_chip {
	struct regmap *regmap;
	struct i3c_device_info info;
};

static int my_i3c_probe(struct i3c_device *i3cdev)
{
	struct my_i3c_chip *chip;

	chip = devm_kzalloc(i3cdev_to_dev(i3cdev), sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i3c_device_get_info(i3cdev, &chip->info);

	chip->regmap = devm_regmap_init_i3c(i3cdev, &my_regmap_config);
	if (IS_ERR(chip->regmap))
		return PTR_ERR(chip->regmap);

	i3cdev_set_drvdata(i3cdev, chip);

	return 0;
}
```

这段代码的重点不在细节，而在思路：

1. `probe()` 收到的是 `struct i3c_device *`
2. 先取 `i3c_device_info`
3. 如果器件是寄存器型设备，优先考虑 `regmap`
4. 之后再决定要不要开 IBI、要不要发私有传输

### 5.1 为什么 `regmap` 值得单独记

本机头文件里可以看到：

```c
#define devm_regmap_init_i3c(i3c, config) ...
```

这意味着：

> 对寄存器型 I3C 设备，Linux 已经给出了标准的 `regmap` 入口。

所以如果设备访问模型是“寄存器地址 + 数据读写”，优先把它收束进 `regmap` 往往比自己手写收发逻辑更整洁。

## 6. IBI 在 Linux 设备驱动里怎么落地

协议上，`IBI` 表示设备在总线内发起事件。  
Linux 里，普通设备驱动如果要接 IBI，主要看这几组 API：

- `i3c_device_request_ibi()`
- `i3c_device_enable_ibi()`
- `i3c_device_disable_ibi()`
- `i3c_device_free_ibi()`

配套的数据结构是：

```c
struct i3c_ibi_setup {
	unsigned int max_payload_len;
	unsigned int num_slots;
	void (*handler)(struct i3c_device *dev,
			const struct i3c_ibi_payload *payload);
};
```

### 6.1 我对这套设计的理解

这套接口有几个特别好的地方：

- 驱动可以显式声明自己能接受的最大 payload
- 驱动可以控制预分配多少个 IBI slot
- handler 由 core 调起，驱动不用自己实现底层仲裁接入

更重要的是，头文件明确写了：

> `handler` 在 workqueue context 里执行，可以睡眠，也允许继续发送总线消息。

这直接影响驱动设计：

- 你不用把它当硬中断 top-half 来写
- 但也不应该在 handler 里干过重的活
- 它更像一个“被 IBI 事件唤醒的可睡眠回调”

### 6.2 设备驱动该怎么用 IBI

我的建议是分成四步：

1. `probe()` 里先读 `i3c_device_info`，确认设备能力
2. 调 `i3c_device_request_ibi()` 建 handler
3. 合适时机调 `i3c_device_enable_ibi()`
4. `remove()` 或 suspend path 里做 disable/free

### 6.3 设备驱动不该关心哪些 IBI 细节

普通设备驱动通常不需要亲自关心：

- IBI slot 怎么从控制器硬件里回收
- 控制器中断怎么转成 IBI payload
- IBI 和总线仲裁怎么衔接

这些更偏 controller driver 侧。

## 7. I3C controller driver 需要实现什么

这部分是“控制器驱动视角”，和普通设备驱动完全不是一个层级。

核心入口是：

```c
int i3c_master_register(struct i3c_master_controller *master,
			struct device *parent,
			const struct i3c_master_controller_ops *ops,
			bool secondary);
```

controller driver 的主体工作就是实现 `struct i3c_master_controller_ops`。

### 7.1 必须重点认识的几个回调

我会先抓住这几个最关键的 mandatory/核心回调：

- `bus_init`
- `do_daa`
- `send_ccc_cmd`
- `priv_xfers`
- `i2c_xfers`

如果硬件支持更多能力，再看：

- `request_ibi`
- `enable_ibi`
- `disable_ibi`
- `free_ibi`
- `recycle_ibi_slot`
- `enable_hotjoin`
- `disable_hotjoin`

### 7.2 `bus_init` 干什么

头文件注释里明确说：

- 至少要在这里调用 `i3c_master_set_info()`
- 还要设置 bus mode
- 也可以顺便做控制器私有初始化

所以 `bus_init` 不是“只是上电复位一下寄存器”，而是 controller driver 向 core 宣告：

- 当前 master 的基本能力
- 当前 bus 的模式
- 当前总线可以开始进入 I3C 管理流程

### 7.3 `do_daa` 的责任边界

这一点我觉得特别重要。头文件注释写得很清楚：

- `do_daa` 要真正发 `ENTDAA`
- 在发现设备后，调用 `i3c_master_add_i3c_dev_locked()`

换句话说：

- controller driver 负责“把设备从线里找出来”
- core 负责“把找到的设备接到 Linux 设备模型里”

这就是 Linux I3C 的典型分工。

### 7.4 `send_ccc_cmd` 和 `priv_xfers`

这两个接口基本对应两类不同事务：

- `send_ccc_cmd`：总线管理面
- `priv_xfers`：设备私有 SDR 数据面

这也是协议视角和 Linux 视角之间最清楚的映射之一。  
在 Linux 里，`CCC` 和 private transfer 就是两条不同的接口路径。

### 7.5 controller driver 面对的是 `i3c_dev_desc`，不是 `i3c_device`

这点需要反复强调。

controller driver 的很多 ops 入口，比如：

- `attach_i3c_dev(struct i3c_dev_desc *dev)`
- `priv_xfers(struct i3c_dev_desc *dev, ...)`
- `request_ibi(struct i3c_dev_desc *dev, ...)`

面对的都是 `i3c_dev_desc`。

这意味着 controller driver 可以：

- 给 descriptor 绑定 controller 私有数据
- 用 `i3c_dev_set_master_data()` / `i3c_dev_get_master_data()`

这和普通设备驱动在 `i3c_device` 上挂 `drvdata` 是两套不同层次的私有数据通道。

## 8. DAA 在 Linux 里的完整映射

协议里，DAA 是一个总线动作。  
Linux 里，DAA 变成了一串分层协作：

1. controller driver 已经通过 `i3c_master_register()` 注册 master
2. core 组织总线初始化
3. controller `ops->do_daa()` 真正发 `ENTDAA`
4. 每发现一个设备，就调用 `i3c_master_add_i3c_dev_locked()`
5. core 为设备分配/更新 `i3c_dev_desc`
6. core 通过相关 `CCC` 填充 `i3c_device_info`
7. core 最终把设备暴露成 `i3c_device`
8. 普通驱动通过 `id_table` 匹配并进入 `probe()`

这段流程说明：

> 对普通设备驱动来说，DAA 发生在 `probe()` 之前。  
> `probe()` 收到设备时，设备已经不是“协议裸对象”，而是“被 core 消化过的 Linux 设备”。

这会直接影响驱动设计。  
普通设备驱动不该一上来再想着自己去做 `ENTDAA`。

## 9. IBI 在 controller driver 里的映射

如果你写的是 controller driver，那么 `IBI` 不是一个抽象事件，而是一条完整的硬件路径：

- 硬件检测到 IBI
- 控制器从线里收下 payload
- 把 payload 放到一个 slot
- 交给 core 排队
- core 在 workqueue 里回调设备驱动的 handler

本机头文件里能看到几组很关键的 helper：

- `i3c_generic_ibi_alloc_pool()`
- `i3c_generic_ibi_get_free_slot()`
- `i3c_generic_ibi_recycle_slot()`
- `i3c_master_queue_ibi()`
- `i3c_master_get_free_ibi_slot()`

这说明 Linux I3C 对 IBI 的建模是：

> IBI 不是一个“立刻执行的同步回调”，而是“带 slot 管理和排队语义的异步事件通道”。

这也解释了为什么设备驱动里的 IBI handler 能睡眠，因为真正的中断硬处理已经被前面几层吸收掉了。

## 10. Hot-Join 在 Linux controller driver 里的位置

从 `i3c_master_controller_ops` 看，Hot-Join 对 controller driver 主要体现在：

- `enable_hotjoin`
- `disable_hotjoin`

而且 `struct i3c_master_controller` 里带有 `workqueue_struct *wq`，注释里还专门提到：

> Hot Join 处理通常需要在可睡眠上下文中延后处理，因为可能要拿 bus lock 做维护动作。

这句话对实现者很重要，因为它说明：

- Hot-Join 不是一个适合在硬中断里直接做完的流程
- 它通常是“硬件先检测到，软件再切到 workqueue 里补完后续管理动作”

## 11. 为什么 Linux 同时保留 I3C 驱动模型和 I2C 驱动模型

I3C 在 Linux 里并不是“彻底替代 I2C 子系统”。  
相反，它是“让一个更现代的 bus model，兼容旧 I2C 世界继续存在”。

从 `struct i3c_master_controller` 里直接带 `struct i2c_adapter i2c` 就能看出这一点。  
从 `struct i3c_bus` 里同时维护：

- `devs.i3c`
- `devs.i2c`

更能看出这一点。

所以我对 Linux I3C 子系统的理解是：

> 它不是要抹掉 I2C，而是在一个总线对象里同时管理 I3C 设备和 legacy I2C 设备。

### 11.1 对普通设备驱动意味着什么

有些芯片可能同时支持 I2C 和 I3C 模式。  
这时 Linux 头文件提供了很有用的一组双注册 helper：

- `i3c_i2c_driver_register()`
- `module_i3c_i2c_driver()`

这类接口很适合：

- 同一颗芯片，板上不同项目可能跑 I2C 或 I3C
- 驱动功能主体相同，只是总线接入方式不同

换句话说，Linux 已经预判了“同一功能驱动需要同时服务 I2C 和 I3C”这种现实。

## 12. `boardinfo` 和固件描述在 Linux 里扮演什么角色

协议层不会关心设备树，但 Linux 必须关心“板上到底宣告了哪些设备、有没有静态地址、有没有希望的初始动态地址”。

因此头文件里专门有：

- `struct i3c_dev_boardinfo`
- `struct i2c_dev_boardinfo`

其中 `i3c_dev_boardinfo` 里很关键的几个字段包括：

- `init_dyn_addr`
- `static_addr`
- `pid`
- `of_node`

这说明 Linux 在建模时并不只依赖“线上的发现结果”，还允许把板级信息叠加进去，比如：

- 某个器件希望优先拿某个动态地址
- 某个器件虽然没有静态地址，但板级已经知道它的 `PID`
- 某个器件在设备树里带了额外资源

所以 DAA 在 Linux 里从来不是完全“裸总线自发现”，而是“总线发现 + 板级知识”的综合过程。

## 13. 协议概念和 Linux 对象怎么对上号

这是我自己最常用的对照表。

| 协议概念 | Linux 里的主要落点 |
| --- | --- |
| Main Master / Current Master | `i3c_master_controller` + `i3c_bus.cur_master` |
| I3C 总线 | `struct i3c_bus` |
| I3C 设备的内部描述 | `struct i3c_dev_desc` |
| 暴露给普通驱动的设备对象 | `struct i3c_device` |
| 设备基础能力信息 | `struct i3c_device_info` |
| DAA | `ops->do_daa()` + `i3c_master_add_i3c_dev_locked()` + core 建模 |
| CCC | `ops->send_ccc_cmd()`，以及 core 组织的一系列 helper |
| 私有 SDR 传输 | `i3c_device_do_priv_xfers()` / `ops->priv_xfers()` |
| IBI | `i3c_device_request_ibi()` 及 controller 的 IBI ops |
| Hot-Join | controller hotjoin ops + core/workqueue 协作 |
| I2C 共存 | `i3c_master_controller.i2c` + `i3c_bus.devs.i2c` |

把这张表记住后，再回头看协议和代码，心里会清楚很多。

## 14. 如果我要读 Linux I3C 代码，我会怎么切入口

### 14.1 如果我是普通设备驱动作者

我优先看这些对象和接口：

- `struct i3c_driver`
- `I3C_DEVICE(...)`
- `i3c_device_get_info()`
- `i3c_device_do_priv_xfers()`
- `i3c_device_request_ibi()`
- `devm_regmap_init_i3c()`

### 14.2 如果我是 controller driver 作者

我优先看这些对象和接口：

- `struct i3c_master_controller`
- `struct i3c_master_controller_ops`
- `i3c_master_register()`
- `i3c_master_set_info()`
- `i3c_master_add_i3c_dev_locked()`
- `i3c_master_do_daa()`
- `i3c_master_queue_ibi()`

### 14.3 如果我是在 debug 枚举问题

我会按这个顺序问自己：

1. `bus_init` 有没有把 controller 能力和 bus mode 设对？
2. `do_daa` 有没有真的把发现到的设备通过 `i3c_master_add_i3c_dev_locked()` 上报给 core？
3. 设备的 `i3c_device_info` 有没有被 core 正确补全？
4. 驱动匹配表是不是和设备真实 `PID/DCR` 对得上？
5. 这是 I3C 设备问题，还是混合总线里的 I2C 兼容问题？

## 15. 我现在对 Linux I3C 子系统的总结

如果只从协议角度看 I3C，很容易把注意力全放在：

- DAA
- CCC
- IBI
- Hot-Join

但到了 Linux 里，更关键的是：

> 这些协议动作到底由谁来负责。

我现在对 Linux I3C 的稳定理解是：

- **controller driver** 负责把协议动作变成硬件事务
- **core** 负责维护总线秩序和设备模型
- **普通设备驱动** 只处理自己的功能，不处理总线治理

这个分层一旦看清，I3C 子系统就不会显得混乱，反而会觉得它设计得相当克制：

- 设备驱动 API 很薄
- controller ops 很明确
- core 把总线级复杂度尽量吸收到了中间层

## 16. 后续继续往下读，最值得补的方向

如果要把这份“Linux 视角版”继续扩下去，我建议优先补三块：

- 真实 controller driver 的实现路径，比如 `dw-i3c-master`、`cdns`、`mipi-i3c-hci`
- 具体设备驱动案例，尤其是同时支持 I2C/I3C 的传感器驱动
- `CCC` 在 Linux core 里的 helper 组织方式，以及哪些 `CCC` 是 core 自动做的，哪些需要驱动显式触发

到这里，这份笔记希望回答的是四个问题：

- 协议概念怎么映射到 Linux 子系统对象
- 普通设备驱动到底该用哪些接口
- controller driver 到底需要实现哪些动作
- Linux 是怎么把 I2C 和 I3C 共存这件事真正落地的


## 同专题阅读

- [MIPI I3C Basic v1.1.1 组内分享](/2026/11/25/i3c-basic-spec/)
- [两根线，一笔事务：I3C 控制器学习手记](/2026/11/25/i3c-controller-guide/)
- [DWC MIPI I3C Target 开发学习文档](/2026/11/25/i3c-target-study/)
