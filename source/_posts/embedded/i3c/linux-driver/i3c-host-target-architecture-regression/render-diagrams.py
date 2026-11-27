#!/usr/bin/env python3
"""Rebuild the article's SVG/PNG figures with Graphviz and a CJK font."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parent
style = '''
graph [bgcolor="#ffffff", pad="0.3", nodesep="0.28", ranksep="0.48",
       fontname="Noto Sans CJK SC", fontsize=21];
node [shape=box, style="rounded,filled", fillcolor="#f1f5f9", color="#94a3b8",
      fontname="Noto Sans CJK SC", fontsize=17, margin="0.18,0.12"];
edge [fontname="Noto Sans CJK SC", fontsize=14, color="#64748b", arrowsize=0.8];
'''
figures = {
    "architecture": '''
digraph G { rankdir=TB;
label="I3C Host / Target：程序、驱动与总线的职责边界"; labelloc=t;
subgraph cluster_h {
 label="Host · Linux / i.MX93"; color="#93c5fd"; style=rounded;
 app [label="Host · Linux / i.MX93\ni3c-host\n命令解析 / 会话 / 自动执行表", fillcolor="#dbeafe"];
 api [label="字符设备 + ioctl\n固定大小 UAPI / poll 等待"];
 client [label="companion 驱动\nowner / session / IBI 事件副本"];
 core [label="I3C core\n设备描述符 / PID / DA / epoch"];
 svc [label="SVC controller driver\nFIFO / CCC / PIO / IBI / HJ"];
 app -> api -> client -> core -> svc;
}
wire [label="SCL / SDA\nPrivate / CCC\nIBI / Hot-Join", shape=ellipse, fillcolor="#fef3c7"];
subgraph cluster_t {
 label="Target · Current Proj / baremetal"; color="#86efac"; style=rounded;
 ip [label="I3C Target IP\nRX/响应队列 / EXT TX queue0"];
 pio [label="Target driver / HAL\nPIO 请求 / 完成 / IBI / 错误"];
 state [label="生命周期与所有权\ngeneration / 状态 / cleanup"];
 runner [label="Target · Current Proj / baremetal\ni3c_test / 自动执行器\n校验 / READY / DONE", fillcolor="#dcfce7"];
 bsp [label="BSP + wrapper\n时钟 / 复位 / PAD / 身份配置"];
 dma [label="独立 DMA 用例\n内存 ↔ I3C 数据端口", fillcolor="#f3e8ff"];
 runner -> state -> pio -> ip [dir=both];
 bsp -> state [style=invis, weight=8];
 bsp -> ip [style=dashed, label="初始化与明确恢复", constraint=false];
 dma -> ip [dir=both, style=dashed, constraint=false];
}
{rank=same;app;runner}
{rank=same;api;bsp}
{rank=same;client;state}
{rank=same;core;pio;dma}
{rank=same;svc;wire;ip}
svc -> wire -> ip [dir=both, constraint=false];
}
''',
    "handshake": '''
digraph G { rankdir=TB;
label="自动回归的一步：READY 交接成功后，才发业务数据"; labelloc=t;
h0 [label="Host：CLAIM / 开启 IBI\n核对 epoch、Target generation", fillcolor="#dbeafe"];
t0 [label="Target：准备 16 B 控制接收\n等待本轮控制帧", fillcolor="#dcfce7"];
{rank=same;h0;t0}
h1 [label="Host：发送 PREPARE\nrun / step / table / generation"];
t1 [label="Target：校验控制帧\n准备本步 RX、TX 或观察器"];
{rank=same;h1;t1}
h2 [label="Host：收到并匹配 READY\n检查事件序号及丢弃计数"];
t2 [label="Target：发布 READY IBI\n跟踪本地通知完成"];
{rank=same;h2;t2}
h3 [label="Host：执行一笔业务\nPrivate Write / Read / CCC"];
t3 [label="Target：处理数据和完成状态\n必要时等待 Host COMMIT"];
{rank=same;h3;t3}
h4 [label="Host：核对 DONE\n才允许进入下一步"];
t4 [label="Target：准备下一控制 RX\n然后发布 DONE IBI"];
{rank=same;h4;t4}
h5 [label="Host：FINISH → 等 FINAL → RELEASE\n分别记录 ret 与 cleanup", fillcolor="#dbeafe"];
t5 [label="Target：完成最终收尾\n检查队列 / active / error", fillcolor="#dcfce7"];
{rank=same;h5;t5}
h0 -> h1 -> h2 -> h3 -> h4 -> h5 [style=invis, weight=20];
t0 -> t1 -> t2 -> t3 -> t4 -> t5 [style=invis, weight=20];
h1 -> t1 [label="Private Write", color="#2563eb", constraint=false];
t2 -> h2 [label="IBI：READY", color="#16a34a", constraint=false];
h3 -> t3 [label="业务请求 / 返回", dir=both, color="#2563eb", constraint=false];
t4 -> h4 [label="IBI：DONE", color="#16a34a", constraint=false];
h5 -> t5 [label="FINISH / FINAL", dir=both, constraint=false];
}
''',
    "lifecycle": '''
digraph G { rankdir=TB;
label="Target 生命周期，与 Host 身份代次分开管理"; labelloc=t;
closed [label="CLOSED\n没有活动外设会话", fillcolor="#e2e8f0"];
prep [label="取得所有权 → 开钟 → wrapper\n复位/PAD → IP 配置 → 发布 ENABLE"];
enabling [label="ENABLING\n写请求已提交，仍需观察确认"];
wait [label="WAIT_DA\nenable_ack 有效，地址尚未建立"];
idle [label="IDLE\nDA 有效 / 队列静止 / 可开始业务", fillcolor="#dcfce7"];
active [label="BUS_ACTIVE / 子请求活动\nRX / TX / IBI / DMA / reset 等"];
closing [label="CLOSING\n请求 disable → 等确认 → BSP finish"];
fault [label="错误锁存 / NEEDS_PLATFORM_RESET\n保留 last_error 与 cleanup_error", fillcolor="#fee2e2"];
identity [label="两端各自编号\nHost epoch：设备描述符代次\nHost session：FD owner 会话\nTarget generation：外设会话代次\nrun / step：执行表中的一次实验", fillcolor="#dbeafe"];
closed -> prep -> enabling;
enabling -> wait [label="使能确认"];
wait -> idle [label="枚举 / DA / quiet"];
idle -> active [label="begin / 执行业务"];
active -> idle [label="完成且收尾"];
idle -> closing [label="无在途请求才 close"];
closing -> closed [label="disable ack + finish 成功"];
active -> fault [label="错误 / 身份失效"];
enabling -> fault [style=dashed, label="非可续等错误"];
fault -> prep [style=dashed, label="明确的外设恢复策略\n不能盲目重发原请求"];
identity -> idle [style=dotted, dir=none];
}
''',
}

for name, source in figures.items():
    source = source.replace("digraph G {", "digraph G {\n" + style, 1)
    (root / f"{name}.dot").write_text(source)
    svg = subprocess.check_output(["dot", "-Tsvg"], input=source.encode()).decode()
    # Avoid ambiguous project-word matches in Graphviz's automatic edge IDs.
    svg = re.sub(r'\bedge(\d+)\b', r'connection\1', svg)
    (root / f"{name}.svg").write_text(svg)
    subprocess.run(["dot", "-Tpng", "-Gdpi=150", "-o", str(root / f"{name}.png")],
                   input=source.encode(), check=True)
    print(name)
