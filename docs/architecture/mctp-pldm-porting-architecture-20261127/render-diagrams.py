#!/usr/bin/env python3
"""Render the architecture illustrations without accessing hardware."""
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

OUT = Path(__file__).resolve().parent
STYLE = '''
graph [rankdir=TB, bgcolor="white", pad=0.25, nodesep=0.35, ranksep=0.5, fontname="Noto Sans CJK SC", fontsize=17, labelloc=t];
node [shape=box, style="rounded,filled", fillcolor="#edf4fb", color="#59748c", fontcolor="#183247", fontname="Noto Sans CJK SC", fontsize=14, margin="0.15,0.12"];
edge [color="#58748b", fontcolor="#334b5f", fontname="Noto Sans CJK SC", fontsize=12, arrowsize=0.7];
'''
DIAGRAMS = {
 'architecture': '''
 graph [label="双端软件架构｜Host 源码已核对；Target 组件与平台集成分开确认"];
 subgraph cluster_h {
  label="i.MX93 Linux Host";color="#9fb6ca";
  h1 [label="用户态：mctp-pldm-request\\nControl 构造 / libpldm Base codec\\n匹配响应、完成码、版本 CRC32"];
  h2 [label="AF_MCTP / SOCK_DGRAM\\nsendto、poll、recvfrom"];
  h3 [label="Linux MCTP core（恢复）\\nEID、Tag、分包、重组、socket 投递"];
  h4 [label="mctp-i3c（回移并适配）\\nnetdev、PID 映射、PEC、IBI 后读取",fillcolor="#fff1dd"];
  h5 [label="I3C core（增加 bus 通知）\\n既有 SVC Controller 驱动",fillcolor="#f1f3f5"];
  h6 [label="I3C Controller 硬件",fillcolor="#f1f3f5"];
  cfg [label="mctp 工具：本地控制路径\\nnetwork、EID、路由、PID 邻居",shape=note];
  cfg -> h3 [style=dashed,label="rtnetlink"];
  {rank=same;cfg;h2;}
  h1 -> h2 -> h3 -> h4 -> h5 -> h6 [dir=both];
 }
 subgraph cluster_t {
  label="Current Proj 裸机 Target";color="#9fb6ca";
  t1 [label="PLDM Base handler + libpldm\\n源码：交付包 on_message()\\n业务 provider 尚未接入"];
  t2 [label="libmctp（固定版本）\\nControl、EID、Tag、分包重组"];
  t3 [label="chip_endpoint I3C binding\\nPEC、16 槽 TX 队列、peek / complete",fillcolor="#fff1dd"];
  t4 [label="平台管理主循环 / glue\\n事件交接、硬件 arm、IBI、RX 重备\\n最新集成源码待核实",style="rounded,dashed,filled",fillcolor="#faf2e9"];
  t5 [label="既有 Target 驱动 / BSP\\nFIFO、IRQ、DMA、cache、时钟复位",fillcolor="#f1f3f5"];
  t6 [label="I3C Target 硬件",fillcolor="#f1f3f5"];
  t1 -> t2 -> t3 -> t4 -> t5 -> t6 [dir=both];
 }
 h6 -> t6 [dir=both,constraint=false,label="同一条 I3C\\nPrivate Write → IBI 0xAE → Private Read"];
 ''',
 'target-lifecycle': '''
 graph [label="Target 交付组件的包生命周期｜平台硬件动作必须独立确认"];
 a [label="libmctp 生成 packet\\nbinding_tx() 在回调内复制"];
 q [label="软件队列 QUEUED\\n拥有独立 MCTP 包副本，不含 PEC"];
 p [label="tx_peek() → PREPARED\\n复制到平台稳定 buffer，追加读 PEC\\nprepared = 本次含 PEC 的长度",fillcolor="#fff1dd"];
 hw [label="平台 arm TX FIFO / DMA\\nbuffer 在真实完成前保持有效",style="rounded,dashed,filled"];
 ibi [label="平台提交 IBI，MDB 0xAE\\n通知 ACK 不代表数据已读走",style="rounded,dashed,filled"];
 read [label="Host Private Read\\n平台记录实际长度与正常结束标志",style="rounded,dashed,filled"];
 ok [label="tx_complete(actual, normal_end)\\nnormal_end 且 actual == prepared？",shape=diamond,fillcolor="#fff1dd"];
 pop [label="清 prepared、队头出队\\n平台确认后可复用硬件 buffer"];
 retry [label="清 prepared、返回 -EIO\\n队头保留；先清硬件，再从包头重备"];
 wait [label="IBI 被拒绝 / 尚未使能\\n不出队，平台处理重试与定时",style="rounded,dashed,filled"];
 a -> q -> p -> hw -> ibi -> read -> ok;
 ok -> pop [label="是"];
 ok -> retry [label="否：短读或异常"];
 retry -> q [label="保留原包"];
 hw -> retry [label="arm 失败：complete(0, false)"];
 ibi -> wait [label="通知未被接受"];
 wait -> ibi [label="按平台策略继续；不直接重复 peek"];
 ''',
 'command-paths': '''
 graph [label="三种处理分支｜枚举成功、MCTP 成功、PLDM 成功分别判断"];
 a [label="Host 的动作"];
 ccc [label="I3C CCC\\nSETDASA / GETPID / GETDCR",fillcolor="#f1f3f5"];
 control [label="MCTP Control 查询\\nMessage Type 0x00"];
 pldm [label="PLDM Base 查询\\nMCTP Message Type 0x01"];
 low [label="I3C 驱动 / 硬件与平台配合\\n不进入 PLDM handler",fillcolor="#f1f3f5"];
 transport [label="I3C Private Write\\nbinding 校验 PEC → MCTP 接收"];
 ctl [label="libmctp Control handler\\nGetEID / GetVersion / GetTypes"];
 app [label="on_message() → PLDM Base\\nGetTID / 版本 / 能力 / 命令位图"];
 local [label="mctp address / route / neigh\\n只配置 Host 本地表项",shape=note,fillcolor="#f1f3f5"];
 a -> ccc -> low;
 a -> control -> transport;
 a -> pldm -> transport;
 a -> local [style=dashed];
 transport -> ctl [label="类型 0x00"];
 transport -> app [label="类型 0x01"];
 {rank=same;ccc;control;pldm;}
'''
}

if __name__ == '__main__':
 for name, body in DIAGRAMS.items():
  data = ('digraph architecture {\n' + STYLE + body + '\n}').encode()
  for fmt in ['svg', 'png']:
   command = ['dot', '-T' + fmt] + (['-Gdpi=150'] if fmt == 'png' else [])
   result = subprocess.run(command, input=data, capture_output=True, check=True)
   output = result.stdout
   if fmt == 'svg':
    # Give generated connector IDs a neutral name for the publishing scan.
    output = re.sub(rb'\bedge([0-9]+)\b', rb'connection-\1', output)
    root = ET.fromstring(output)
    graph = root.find('{http://www.w3.org/2000/svg}g')
    assert graph is not None and graph.attrib['transform'].startswith('scale(1 1)')
   else:
    assert result.stdout.startswith(b'\x89PNG\r\n\x1a\n')
   (OUT / (name + '.' + fmt)).write_bytes(output)
  print(name + ': SVG / PNG generated')
