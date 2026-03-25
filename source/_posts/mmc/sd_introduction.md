---
title: sd_introduction
date: 2026-03-02
toc: true
max_depth: 3 
categories: embedded_tech
tags: [embedded , Linux]
---



# 写在前面

学习SD/SDIO/EMMC的第一步，首先是搞清楚他们之间的区别，摸清不同的技术路线之间的差异  SDIO 设备(WIFI, BT, GPS等)，SDCard和eMMc都挂在SDHCI控制器下, 三者都“共用同一物理总线接口”,   但协议栈、用途、封装完全不同:

 

| 维度               | SD 卡                            | SDIO 卡                    | eMMC                         |
| ------------------ | -------------------------------- | -------------------------- | ---------------------------- |
| 官方全称           | Secure Digital Memory Card       | Secure Digital I/O Card    | embedded MultiMedia Card     |
| 核心作用           | 大容量可插拔存储                 | 外设接口（Wi-Fi/BT/GPS）   | 板载闪存                     |
| 封装/形态          | 可插拔 TF/SD/micro-SD            | 可插拔 TF/SD               | BGA 153/169 ball 焊板        |
| 组织规范           | SD Association (SDA)             | SD Association (SDA)       | JEDEC                        |
| 物理层接口         | SD Bus                           | SD Bus                     | MMC Bus                      |
| 数据位宽           | 1/4-bit（无 8-bit）              | 1/4-bit（无 8-bit）        | 1/4/8-bit                    |
| 最高理论速率       | SD 3.0 SDR104：104 MB/s          | 同上                       | eMMC 5.1 HS400：400 MB/s     |
| 信号形态           | 单端 LVC MOS（+UHS-II LVDS）     | 同上                       | 单端 LVC MOS                 |
| 专用命令           | 存储块读写 CMD17/18/24/25        | CMD5、CMD52、CMD53         | 块读写 + EXT_CSD             |
| 寄存器差异         | CID/CSD/SCR/SSR                  | CCCR/FBR/CIS（CIA）        | CID/CSD/EXT_CSD              |
| 中断机制(物理引脚) | 无                               | DAT1 复用 IRQ              | 无                           |
| 安全/DRM           | CPRM/AES 可选                    | 厂商自定                   | 无                           |
| 容量范围           | 128 MB–2 TBClass 2/4/6/10, U1/U3 | 通常无容量或 2–8 GB combo  | 常用：2 GB–256 GB最大：2TB   |
| 热插拔             | ✅                                | ✅                          | ❌（焊板）                    |
| 典型应用           | 相机、行车记录仪、消费级存储     | Wi-Fi 6 模组、BT、GPS、UWB | 手机、平板、机顶盒、车载中控 |

SD Card和SDIO设备共用SD Bus，总线宽度最大为4bit。SD Card走SD Memory协议，SDIO设备走SDIO协议。SDIO协议规范可查询 SD 协会官方文档：  eMMc使用MMC Bus，总线宽度最大为8bit。eMMc走MMC协议。MMC协议规范可以查询JEDEC协会的官方文档  

另外，需要搞清楚SD/SDIO规范族内几个文档的差异，这个规范族里有不 同分册，各司其职，又相互引用：



SD Specifications（总族）  

├─ Part 1   Physical Layer Simplified Specification（物理层：引脚、电气、命令、时序）   

├─ Part A2  SD Host Controller Simplified Specification（主机控制器寄存器级规范）   

├─ Part E1  SDIO Simplified Specification（SDIO 协议本身：Function、CCCR/FBR/CIS、CMD5/52/53）  /   

├─ Part E2  SDIO Bluetooth Type-A Simplified Specification（SDIO-蓝牙子规范）  

├─ Part E7  iSDIO Simplified Specification（无线 LAN SDIO 扩展） 

├─ Security Simplified Addendum（内容保护、加密命令）  

└─ File System Specification（FAT/exFAT 布局） 

1. Part E1（SDIO Simplified Specification） 是 SDIO 的“核心册”，所有 SDIO 卡必须遵守。  
2. Part 1（Physical Layer） 是 E1 的前置依赖——引脚定义、时钟、CMD/RESP 帧格式、CRC 等都在 Part 1，E1 只给出“SDIO 在这些基础上新增/修改 了什么”。 
3. Part E2/E7 等 是 E1 的“应用补充”，分别给蓝牙、Wi-Fi 等外设规定统一的寄存器接口，方便“插上就用”。 



# SD Memory Card

## 一，简介

SD 是一种记忆卡（memory card），是为音视频消费电子设备提供安全，大容量，高性能和便携 的一种存储卡。用户可以选择对卡内容进行加密保 护。也支持通用加密标准，以支持移动电子商务  和数字系统应用。 SDIO 的 memory card 软硬件兼容 SD 卡，支持 SDIO 协议。 SD 的规格书细分为几个文档，见下图



![image-20260302172304674](sd_introduction/image-20260302172304674.png)

主要设计应用文档，文件系统文档，安全文档，物理接口规格书文档（基带一般只要关注这个 文档）和 移动商务扩展规格文档

按照卡容量来分类： 

​	a. 标准容量卡（SDSC）：不超过 2GB 的卡   

​	b. 高容量卡（SDHC）：大于 2GB 又不超过 32GB 的卡   

​	c. 扩展容量卡（SDXC）：大于 32GB 又不超过 2TB 的卡  

只有 Host 和文件系统支持 2.0 协议以上才可以访问 SDHC 卡。其他协议在初始化 SDHC 卡时， 会失败。   

只有 Host 和文件系统支持 3.0 协议以上才可以访问 SDXC 卡。  

工作电压：

​	a. 高电压 SD 卡——工作电压范围为：2.7～3.6V  

​	b. 双电压记忆卡，可以工作在低电压范围（TBD, 1.70 V – 1.95 V）和 2.7~3.6V 范围。  

总线速率模式（用在 4 线制总线）（注：UHS-I 的卡支持 1.8V 的 IO 信号）   

​	a. 默认速率模式：3.3V 信号，高达 25MHz，数据速率 12.5MB/S   

​	b. 高速率模式：3.3V 信号，高达 50MHz，数据速率 25MB/S    

​	c. UHS-I SDR12：1.8V 信号，高达 25MHz，数据速率 12.5MB/S    

​	d. UHS-I SDR25：1.8V 信号，高达 50MHz，数据速率 25MB/S    

​	e. UHS-I SDR50：1.8V 信号，高达 100MHz，数据速率 50MB/S    

​	f. UHS-I SDR104：1.8V 信号，高达 208MHz，数据速率 104MB/S    

​	g. UHS-I DDR50：1.8V 信号，高达 50MHz，双时钟沿采样数据，数据速率 50MB/S    

SDR——Single Data Rate；DDR——Double Data Rate。   

支持热插拔  机械写保护  插拔识别  安全保护  

SD 的外形规格   

​	a. 标准SD    

​	b. MiniSD，即相机中常见的一体式的 SD 卡   

​	c. MicroSD，即手机中使用的 TF 卡

![image-20260302172504252](sd_introduction/image-20260302172504252.png)

速率级别： 

定义了 5 个速率级别，用于指示卡最差性能。   

​	a. Class 0——没有什么特定性能   

​	b. Class 2——速率大于等于 2MB/S    

​	c. Class 4——速率大于等于 4MB/S    

​	d. Class 6——速率大于等于 6MB/S   

​	e. Class 10——速率大于等于 10MB/S    SDHC 和 SDXC 卡都应该支持速率级别，且在 Class 2 以上。 



注：协会规定的速度等级（Speed Class），在测试方法上与普通的性能测试有较大区别。测试时 根据卡片内的碎片的不同程度分别测试读速率曲线和 写速率曲线，而并不是对单一大文件的传输速度进行测试。 SD 协会定义了满足各等级最低要求的性能曲线，根据测试所得的数据和 SD 协会规定的性 能标准进 行比较，从而判断卡片的性能等级。  一个 50MHz 的时钟写操作，其速率等级不能简单这样计算：50×4/8=25M/S；写过程涉及写命令 （多块写比单块写效率高）相、响应相、写相（Busy  期间、CRC 传输期间）和停止命令等等，这些都会影响速率 等级。一般50MHz时钟的总线，最多能支持10M/S（Class 10）写带宽，读带宽比较高。   其实真正测试中应该测试应用层读写带宽。即传输一个大文件进行测试



![image-20260302172607342](sd_introduction/image-20260302172607342.png)





## 二，总线拓扑与引脚

SD模式  定义了两种通讯协议：SD 和 SPI。SD卡根据 Host 的初始化时的请求，而选择哪种模式。通常一个 SD 总线直连接一个 SD 卡。

![image-20260302172700585](sd_introduction/image-20260302172700585.png)

包括下面信号：  

​	CLK：Host 给卡的时钟信号   

​	CMD：双向信号的命令/响应信号  

​	Data0~3：4 根双向数据信号   

​	Vdd，Vss1，Vss2：电压和地信号   

在 Default speed，SD 总线可以 1 个主（Host 控制器），多个从（卡）。但是高速和 UHS-I，只能单主和单从模式。   对于 1 主多从方式，通过初始化中得到的逻辑地址来区别各卡（注：命令参数中含有地址信息，各卡发现是自己的数据才响应）。   上电后，默认是单线制（Data0），初始化时，Host 先查询卡是否支持 4 线制，如果支持，则通过 CMD 命令将总线切换至 4 线制。 

 SPI模式用的很少，不做讲解  SD模式下的PIN： 



![image-20260302172746752](sd_introduction/image-20260302172746752.png)



1. S：供电；I：Input；O：Push-Pull 驱动方式的 Output；PP：Push-Pull 驱动方式的 I/O    
2. 扩展的数据线（Data1~3）在上电时默认是输入，当 Set_Bus_Width 命令执行后作为数据线 使用。   
3. 上电后，卡内的这个数据线 50KOHM 的上拉电阻使能了（实际上是 20K～90KOHM 范围 内的电阻）。这个电阻用于卡检测和模式选择。Host 驱动 这个 Data3 为高电平或者让其 上拉为高电平来选择 SD 模式。如果 Host 想选择 SPI 模式，应该驱动它为低电平。Host 检测到这个信号拉高时，判 断为卡插入。在正常数据传输过程，软件应该用 Set_Clr_Card_Detect（ACMD42）命令将这个电阻断开。   
4. 在 SDIO 模式，Data1 在中断相（非数据传输相）可以作为卡给 Host 的中断信号。   5. 在 SDIO 模式，Data2 可以用于读等待信号 



TODO: 补充一套原理图



## 三，总线协议

SD 总线是基于命令和数据流，它们由一个开始 Bit 发起，又一个停止 Bit 结束。   

- Command：命令开始一个操作。命令由 Host 驱动，或者给单卡（寻址命令），或者给所有连接的卡（广播命令）。命令通过 CMD 信号线串行方式 传输。
- Response：响应由响应的卡驱动，或者同时由连接的卡驱动。作为之前接收到的命令的应答。响应也是通过 CMD 信号线串行方式传输。
- Data：数据可以由 Host 或者卡驱动，通过数据线传输 

![image-20260302173025521](sd_introduction/image-20260302173025521.png)

在初始化时，确定卡的 Address。 数据传输是以块（Block）为单位。数据块后面为 CRC 校验位。定义了单块和多块传输操作。 多块传输通过 Stop 命 令来结束

![image-20260302173038098](sd_introduction/image-20260302173038098.png)



块写操作期间通过 Data0 信号线指示 Busy 状态。

![image-20260302173046917](sd_introduction/image-20260302173046917.png)

命令内部的编码方式

![image-20260302173056827](sd_introduction/image-20260302173056827.png)



每一个命令由一开始 Bit 发起（‘0’）和一结束 Bit 结束（‘1’），整个 Bit 长度为 48 Bit。每个命令由 CRC 校验做保护。当传输过程发生校验错误，可以 重新发起这个命令。  响应有四个编码方式，取决于响应内容。长度为 48 或者 136Bits。采用 16-bit CCITT 校验方式。



![image-20260302173110913](sd_introduction/image-20260302173110913.png)

CMD 信号先传 MSB 位，后传 LSB 位。   当采用 4 线制总线时，各信号线开始位、停止位和 CRC 都是独立。CRC 校验的计算和检查是 按信号线处理的。而 CRC 的状态响应和 Busy 指示由卡 通过 Data0 送给 Host. 

TODO: SD 卡有两种数据表格式补充



## 四，设备寄存器

每个SD卡都有一组寄存器：

![image-20260302173209304](sd_introduction/image-20260302173209304.png)

通过命令操作这些寄存器，以达到控制SD Card行为，读写数据的目的。比如：Host 可以通过给卡上下电硬复位卡。卡上电后，其上电检测电路会自行 将卡处于一个预定义的状态。软件可以通过 CMD0（Go_Idle）命令软复位卡。 



## 五，UHS-I卡的系统带宽和速率模式

系统带宽取决于卡和 Host 支持的速率模式。(**与**关系)

### UHS-I 卡的初始化命令顺序

当上电后，卡为 3.3V 信号模式。首先 CMD0 选择总线模式：SD 模式或者 SPI 模式。1.8V IO 信号模式只能在 SD 模式进入。一旦进入了 1.8V 信号模 式， 就不能进入 SPI 总线模式和 3.3V 信号模式，除非重新下电上电。如果卡接受 CMD0，卡返回到 Idle状态，但是仍然运行在 SDR12 的时序中。因为更高的速率要求低电压信号，对于 SDR50，DDR50 和 SDR104 模式，UHS-I 调节成 1.8V信号。卡仍然由 3.3V 供电，但是 SDCLK、CMD 和 Dat[3:0] 从 3.3V 修改为 1.8V 级别。为了避免Host 和卡直接电压错误匹配，信号电压级别切换在初始化的电压切换时序中完成。CMD11 发起电压切换时序。卡 进入 UHS-I 模式，其输入输出时序被改变。UHS-I 只能应用于 SD 的 4 线制模式，所以 Host 应该用 ACMD6 将卡设置成 4 线制。当卡进入 USH-I 模 式，不管有无ACMD6 执行，卡都将切换到 4 线制模式。Host 可以通过 CMD6 的功能组 3 选择卡的合适的输出驱动强度。Host 可以通过 CMD6 的功能 组 1 选择某一 UHS-I 模式。每一个 UHS-I 模式定义了兼容当前存在的卡的最大频率，采样沿（上升沿或者双沿）和最大电流消耗。Host 选择哪种  UHS-I 模式，取决于其能提供的 SDCLK 频率和供电能力。在 SDR50 或者 SDR104 模式选择后，UHS-I Host 可以用调节命令（CMD19）调节采样点。 DDR50模式并不支持调节命令。

![image-20260302184834266](sd_introduction/image-20260302184834266.png)



### Tuning调谐

下图为典型的 UHS-I Host 系统。Host 有时钟发生器，其提供 SDCLK 给卡。

当写操作时，由于时钟驱动方向和数据驱动方向相同，写数据可以传输同 步于 SDCLK 信号，而不管传输线的延迟现 象。

在读操作时，时钟驱动方向和数据正好相反，Host 接受的读数据被往返延时，输出延时和 Host卡等待而延时。所以对于 Host，接收数据是最关键的。因此，**Host 需要采样时钟发生器来接受响应、CRC 状态和读数据块**。

![image-20260302184929005](sd_introduction/image-20260302184929005.png)

下面介绍两种不同的采样方式：

#### Host 可变采样 

Host 用采用时钟发生器去决定正确的采样点。Host 使用卡预定义在调节块中参数作为一种找到采样点的帮助。Host 使用 CMD19 条件命令来读取调节 块。 这个方法应用于整个频率范围。在小于 25MHz 频率时，Host 不要调节地访问卡。

####  Host 固定采样 

Host 使用预先定义 hide 采样点。这个方法可以用于高达 100MHz。Host-SDR-FD 通过时钟回环 方式得到采样时钟。对于小于 100MHz 频 率范围，UHS50 和 UHS104 应该满足 Todly （max）输出延时约束。



## 六，典型卡操作

要操作卡，首先要了解HOST与卡之间的通信机制： 

Host 和卡之间的通信都是由 Host 来控制的。

Host 发出两种类型的命令：广播和寻址（点对点） 命令。    

- 广播命令 ：给所有的卡，有些这样的命令要求响应。  
- 寻址命令 ：寻址命令发给有地址的卡，卡将返回响应。 



下面是 SD 记忆卡系统的两个操作模式（Host 和卡）：  

- 卡识别模式 ：复位后，Host 处于卡识别模式，寻找总线上的新卡。卡将处于这个模式，直到接收到 Send_RCA 命令（CMD3）。   
- 数据传输模式 ：卡的 RCA 首次发布后，卡进入数据传输模式。Host 识别完毕总线上的所有卡后，进入数据传输模式。  

操作模式和卡的状态之间的关系。SD 记忆卡的状态图中的每一个状态处于某一个操作模式中。

![image-20260302185850404](sd_introduction/image-20260302185850404.png)



### 6.1 卡复位

命令 Go-Idle_State（CMD0）是软件复位命令，不管当前卡的状态，设置卡进入 Idle State。在Inactive State 卡不受这个命令的影响。Host 给卡上电 后，所有的卡进入 Idle State，包括以前处于Inactive State 的卡。   上电或者 CMD0 后，所有的卡的 CMD 信号线处于输入模式，等待下一个命令的开始位。卡被初始化为默认相关卡地址（RCA=0x0），和一个默认驱动 状态寄存器设置（最低的速率，最高驱动电流能力）。





### 6.2 运行条件确认 

在 Host 和卡开始通讯时，Host 可能不知道卡支持的电压，卡可能不知道是否其支持当前的提供的电压。Host 在特定电压下发出复位命令（CMD0）， 假设卡可以支持。 SD 的物理层规格版本 2.0新定义了 CMD8（Send_If_Cond）来确认电压。CMD8被用于确认 SD 记忆卡接口操作条件。卡通过分析 CMD8 的参数来  Check运行条件，Host 通过分析 CMD 的响应来 Check。  HOST在参数的 VHS 域指示可提供的电压。卡认为 VHS定义的电压是当前提供的电压。在任何时间 VHS 只能有 1Bit 设置成‘1’。CRC 和 Check 方案都用 于 Host 去 Check Host 和卡之间的通讯有效。  如果卡能运行在提供的电压，响应回返命令参数中的电压和 Check 方案。如果卡不能运行在提供的电压，卡不会有相应，仍然处于 Idle State。  对于初始化 SDHC 卡，强制先于第一个 ACMD41执行 CMD8。接受 CMD8 让卡认识到 Host 支持 The Physical Layer  Specification Version 2.00，卡就可 以使能新功能。 对于低压 Host，强制先于 ACMD41 发送 CMD8。在双电压卡不接受 CMD8 时，它将像只高电压卡一样运行，在低压 Host 不发送 CMD8 时，卡将在  ACMD41 命令时进入 Inactive 状态。  Sd_Send_Op_Cond（ACMD41）被设计用于提供给 Host 一个机制来鉴别和拒绝不匹配 Host 希望的 Vdd 电压范围的卡。Host 将要求 Vdd 电压范围作 为命令的参数发出。不能在定义范围执行数据传输的卡应该从将来的总线操作中 Discard 它们自己，进入 Inactive State。OCR 寄存器的 Level应该相应 地定义。 注意：ACMD41 是应用命令，因此 APP_CMD（CMD55）总是先于 ACMD41（注：CMD55 + CMD41=ACMD41）。  在 Idle_State 中用于 CMD55 中的 RCA 应该是卡默认 RCA（0x0）。 在 Host 激发复位命令（CMD0）复位卡后，Host 应该先于 ACMD41 激发 CMD8  来重初始化 SD 记忆卡。在将超出电压范围的卡进入 Inactive 状态之前，通过在 ACMD41 的参数中将 OCR 设置成 0，Host 可以询问每一个卡，继而 决定通用电压。如果 Host 能够选择一个通用电压范围，或者应用知晓 Stack 中没有可以用的卡，这个询问应该可以用。 如果 ACMD41 作为询问激发，卡不会发起初始化，会忽略参数中的 HCS 位。此后，Host 可以选择运行电压，重新激发 ACMD41，将不兼容的卡在将超 出电压范围的卡进入 Inactive 状态之前，通过在 ACMD41 的参数中将 OCR 设置成 0，Host 可以询问每一个卡，继而决定通用电压。如果 Host 能够选 择一个通用电压范围，或者应用知晓 Stack 中没有可以用的卡，这个询问应该可以用。如果 ACMD41 作为询问激发，卡不会发起初始化，会忽略参数中 的 HCS 位。此后，Host 可以选择运行电压，重新激发 ACMD41，将不兼容的卡Send 成 Inactive State。  在初始化过程，Host 不允许变化运行电压范围。

![image-20260302190019913](sd_introduction/image-20260302190019913.png)



### 6.3 卡初始化与识别流程

总线激活后，主机启动卡的初始化与识别流程。

#### 初始化流程启动：ACMD41的核心作用

初始化流程以发送“SD_SEND_OP_COND命令（即ACMD41）”开始，发送时需设置卡的操作条件及操作条件寄存器（OCR）中的主机容量支持位（HCS）。其中，HCS位的含义如下：

- HCS位设为1：表示主机支持高容量SD存储卡（SDHC）；
- HCS位设为0：表示主机不支持高容量SD存储卡（SDHC）。

#### CMD8对ACMD41功能的扩展与响应规则

主机发送CMD8后，ACMD41的功能会扩展：其命令参数中包含HCS位，响应中包含卡容量状态位（CCS）。具体响应逻辑如下：

- 未响应CMD8的卡，会忽略HCS位；若卡未响应CMD8，主机需将HCS位设为0；
- 标准容量SD存储卡（SDSC）会直接忽略HCS位；
- 若HCS位设为0，高容量SD存储卡（SDHC）永不返回就绪状态，始终保持忙位为0。

#### 忙位的作用与ACMD41的重复发送规则

OCR中的“忙位”用于卡向主机反馈ACMD41初始化是否完成：

- 忙位设为0：表示卡仍在初始化中；
- 忙位设为1：表示初始化完成。

主机需重复发送ACMD41，直至满足以下任一条件：

① 从首次发送带电压窗口参数的ACMD41开始计时，时长至少1秒；

② 卡的忙位设为1。

同时需注意：

- 卡仅在“首次接收带电压窗口参数的ACMD41”时，才会检查OCR中的操作条件与HCS位；
- 重复发送ACMD41期间，主机除CMD0外，不得发送其他命令。

#### CCS位的含义（仅CMD8有响应时生效）

若卡响应CMD8，ACMD41的响应中会包含CCS（卡容量状态）字段，且仅当卡返回就绪状态（忙位=1）时，CCS位才有效：

- CCS=1：表示该卡为高容量SD存储卡（SDHC）或扩展容量SD存储卡（SDXC）；
- CCS=0：表示该卡为标准容量SD存储卡（SDSC）。

#### 识别流程：从CID获取到RCA分配

#### 初始化序列的通用性与不兼容卡处理

主机对系统中所有新接入的卡执行相同的初始化序列，不兼容的卡会被送入“非活动状态”。

#### 发送CMD2（ALL_SEND_CID）获取CID

主机向每张卡发送“ALL_SEND_CID命令（即CMD2）”，以获取卡的唯一标识（CID，卡识别号）：

- 处于“未识别状态（即就绪状态）”的卡，会通过CMD线返回CID作为响应；
- 卡发送CID后，状态切换至“识别状态”。

#### 发送CMD3（SEND_RELATIVE_ADDR）获取RCA

随后，主机发送“SEND_RELATIVE_ADDR命令（即CMD3）”，请求卡分配新的“相对卡地址（RCA）”——RCA长度短于CID，后续数据传输中用于寻址该卡：

- 主机接收RCA后，卡的状态切换至“待机状态”；
- 若主机需重新分配RCA，可再次发送CMD3，卡的“实际RCA”以最后一次分配的数值为准。

#### 识别流程的重复执行

主机需对系统中的每张卡，重复执行“CMD2→获取CID→CMD3→获取RCA”的识别循环。

#### SDXC卡的特殊说明

- 初始化兼容性：SDXC卡的初始化流程与SDHC卡完全相同；
- 容量要求：SDXC卡的用户区容量由C_SIZE（容量大小）字段指定，且容量需≥32GB。

#### 应用说明（Application Notes）

当卡始终不返回就绪状态时，主机需将ACMD41的超时时间设为1秒以上，以终止ACMD41的重复发送操作。超时计时从“首次发送带电压窗口参数的ACMD41”开始。



![image-20260302191030253](sd_introduction/image-20260302191030253.png)



### 6.4 初始化命令 （ACMD41）

ACMD41 的参数增加了两个新 Field。  XPC 用于给 DS 或者 SDR12 速率的 SDXC 卡的功率控制。如果 Host 设置 XPC 为‘0’，SDXC 卡运行小于 100mA，但是不支持速率级别（Class 0 在  SD 状态寄存器中标识）；如果‘1’，卡运行 小于 150mA，同时支持速率级别。   UHS-I 支持 Host 设置 S18R=1 来询问卡切换到 1.8V 电平的 IO 信号。UHS-1 卡在 ACMD41 的 响应中设置 S18A=1 表示支持。继而 Host 可以激发电 压切换命令（CMD11）。  

下图为ARG：

![image-20260302191104499](sd_introduction/image-20260302191104499.png)



下图为RESP:

![image-20260302191118025](sd_introduction/image-20260302191118025.png)



#### 总线电压切换序列

![image-20260302191140331](sd_introduction/image-20260302191140331.png)

初始化流程图：

![image-20260302191418091](sd_introduction/image-20260302191418091.png)

当信号电平级别是 3.3V 时，Host 重复激发 ACMD41（HCS=1 和 S18R=1）直到响应表明 Read。第一个 ACMD 的参数 HCS 和 S18R 有效，但是后续 ACMD41 也要带相同参数。如果响应的 Busy位指示 Read，Host 需要检查 CCS 和 S18A。S18A=0 表示不允许电压切换，Host 需要使用当前信号电平级别；反之，可以。Host 激发 CMD11 来发起电平切换顺序。接受 CMD11 后，卡返回 R1 类型相应，开始电压切换顺序。没有 CMD11 的响应意味着 S18A 为 0，因此 Host 不应该激发 CMD11。通过检查 Dat[3:0]变为高电平 check 电压切换顺序的完成，Dat[3:0]的哪些 Bit 用于 check 取决于 Host的能力。对于 UHS-I 卡，通过 CMD6 功能组 3 来选择卡相应的驱动强度，通过 CMD6 的功能组 1 选择某一种 UHS-I 模式。



####  切换信号电压的时序

协商部分： 

在初始化过程时钟频率范围应该为 100KHz～400KHz（linux内核下会遍历尝试400K->300K->200K->100K）。下表示意对于 ACMD41 的切换信号电压 的命令（S18R）-响应（S18A）组合。S18R 是 ACMD41 的 命令参数，用于 Host 查询信号电压切换。S18A 在响应中，用于指示卡接受电压切换（电压 并不在这里切换）。 

**如果信号电平已经是 1.8V，S18R 会被忽略（无论你带或不带S18R），S18A=0 总是返回0，意味着当前信号电平保持。**

![image-20260302191628683](sd_introduction/image-20260302191628683.png)



#### 电压协商 & 电压切换软件实现

协商

```

static int sd_send_op_cond(struct mmc *mmc, bool uhs_en)
{
	int timeout = 1000;
	int err;
	struct mmc_cmd cmd;

	while (1) {
		cmd.cmdidx = MMC_CMD_APP_CMD;
		cmd.resp_type = MMC_RSP_R1;
		cmd.cmdarg = 0;

		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;

		cmd.cmdidx = SD_CMD_APP_SEND_OP_COND;
		cmd.resp_type = MMC_RSP_R3;

		/*
		 * Most cards do not answer if some reserved bits
		 * in the ocr are set. However, Some controller
		 * can set bit 7 (reserved for low voltages), but
		 * how to manage low voltages SD card is not yet
		 * specified.
		 */
		cmd.cmdarg = mmc_host_is_spi(mmc) ?
				     0 :
				     (mmc->cfg->voltages & 0xff8000);

		if (mmc->version == SD_VERSION_2)
			cmd.cmdarg |= OCR_HCS;

		if (uhs_en)
			cmd.cmdarg |= OCR_S18R;

		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;

		if (cmd.response[0] & OCR_BUSY) {
			break;
		}

		if (timeout-- <= 0)
			return -EOPNOTSUPP;

		usleep(1000);
	}

	if (mmc->version != SD_VERSION_2)
		mmc->version = SD_VERSION_1_0;

	if (mmc_host_is_spi(mmc)) { /* read OCR for spi */
		cmd.cmdidx = MMC_CMD_SPI_READ_OCR;
		cmd.resp_type = MMC_RSP_R3;
		cmd.cmdarg = 0;

		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;
	}

	mmc->ocr = cmd.response[0];
	if (uhs_en && !(mmc_host_is_spi(mmc)) &&
	    (cmd.response[0] & 0x41000000) == 0x41000000) {
		err = mmc_switch_voltage(mmc, MMC_SIGNAL_VOLTAGE_180);
		if (err)
			return err;
	}

	mmc->high_capacity = ((mmc->ocr & OCR_HCS) == OCR_HCS);
	mmc->rca = 0;

	return 0;
}

```

切换：

```

int mmc_switch_voltage(struct mmc *mmc, int signal_voltage)
{
	struct mmc_cmd cmd;
	int err = 0;

	/*
	 * Send CMD11 only if the request is to switch the card to
	 * 1.8V signalling.
	 */
	if (signal_voltage == MMC_SIGNAL_VOLTAGE_330)
		return mmc_set_signal_voltage(mmc, signal_voltage);

	cmd.cmdidx = SD_CMD_SWITCH_UHS18V;
	cmd.cmdarg = 0;
	cmd.resp_type = MMC_RSP_R1;

	err = mmc_send_cmd(mmc, &cmd, NULL);
	if (err)
		return err;

	if (!mmc_host_is_spi(mmc) && (cmd.response[0] & MMC_STATUS_ERROR))
		return -EIO;

	/*
	 * The card should drive cmd and dat[0:3] low immediately
	 * after the response of cmd11, but wait 100 us to be sure
	 */
	err = mmc_wait_dat0(mmc, 0, 100);
	if (err == -ENOSYS)
		usleep(100);
	else if (err)
		return -ETIMEDOUT;

	/*
	 * During a signal voltage level switch, the clock must be gated
	 * for 5 ms according to the SD spec
	 */
	mmc_set_clock(mmc, mmc->clock, MMC_CLK_DISABLE);

	err = mmc_set_signal_voltage(mmc, signal_voltage);
	if (err)
		return err;

	/* Keep clock gated for at least 10 ms, though spec only says 5 ms */
	/* TODO: Hardware need to be perf in decrease following delay */
	usleep(80000);
	mmc_set_clock(mmc, mmc->clock, MMC_CLK_ENABLE);

	/*
	 * Failure to switch is indicated by the card holding
	 * dat[0:3] low. Wait for at least 1 ms according to spec
	 */
	err = mmc_wait_dat0(mmc, 1, 1000);
	if (err == -ENOSYS)
		usleep(1000);
	else if (err)
		return -ETIMEDOUT;

	return 0;
}

```



电压切换部分的时序图：

为了在 Host 和卡之间同时切换信号电平级别，信号电压切换过程由 CMD11 发起，见下图 。只有当 ACMD41 的响应的 S18A=1 才会激发 CMD11。 

![image-20260302192102670](sd_introduction/image-20260302192102670.png)

1） Host 激发 CMD11 开始发起电压切换过程

2） 卡返回 R1 类型响应

3） 在响应之后，卡立刻驱动 Dat[3:0]为低电平

4） Host 停止驱动 SDCLK 后，卡开始切换电压到停止 SDCLK 的时间并没有定义。Host 通过 Check CMD 信号或者 Dat[3:0]的信号电平来 Detect 切换过程是否开始，哪个信号应该用于 Check 取决于 Host 的能力。如果低电平没有 Detect 到，Host 应放弃切换过程，执行上电时序

5） 卡的 1.8V 电源应该在 5mS 内稳定输出。Host 保持 SDCLK 为低电平至少 5mS。这意味着，对卡，5mS 是最大值；而对于 Host，5mS 是最小值。

6） 从 4）开始的 5mS 后，Host 的电源稳定输出，Host 开始提供 1.8V 的 SDCLK。卡可以 Check SDCLK 是否为 1.8V。

7） 通过检测 SDCLK，卡驱动 CMD 为 1.8V 至少一个时钟，接着停止驱动（高阻态）。CMD触发在时钟 SDCLK 的上升沿（SDR 时序）。

8） 卡能够 Check Host 是否通过上拉电阻将 CMD 驱动为 1.8V。

9） 如果成功地完成 1.8V 信号电压切换，卡驱动 Dat[3:0]为高电平至少 1 个时钟，接着停止驱动（高阻态）。Dat[3:0]触发在时钟 SDCLK 的上升沿（SDR 时序）。

10） Dat[3:0]应该在提供 1.8V 的 SDCLK 的 1mS 内变为高电平。Host 在提供工 SDCLK 后的 1mS 去 检测 Dat[3:0]是否为高电平。这意味着，对卡，1mS 是最大值；而对于 Host，1mS 是最小值。这个切换过程完成后，Host 和卡用 SDR12 时序通讯



**电压切换错误检测时序**

![image-20260302192219999](sd_introduction/image-20260302192219999.png)

A） 如果卡在（5）-（8）检测到电压错误，卡保持驱动 Dat[3:0]为低电平知道卡下电

B）CMD 可能低电平或高阻态

C）Host 在开始提供 SDCLK 的 1mS 后 check Dat[3:0]是否为高电平

D） 如果 Dat[3:0]为低电平，Host 驱动 SDCLK 为低电平，接着停止给卡供电

卡应该 check 自己的电源的电压和 Host 的信号小于 2.5V。错误发生通过（A）和（B）指示。





### 6.5 数据传输模式

todo!



## 总结

主要介绍了sd card相关, sd协议相关的内容。sd card的上电，协商，电压切换的时序与流程需要特别关注

后续会继续介绍：

1. 高速Tuning介绍以及软硬件实现
2. write phase以及获取方式
3. cadence sdhc控制器以及驱动讲解（关注中断，tuning，异常处理，与BLK层关系）
4. cqhci机制







































