#!/usr/bin/env python3
"""Regenerate this article's six SVG/PNG diagrams with system Graphviz."""
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

OUT = Path(__file__).resolve().parents[1] / 'source/_posts/i3c/mctp-pldm-topologies-from-simple-to-complex'
STYLE = '''graph [rankdir=TB, bgcolor="white", pad="0.25", nodesep="0.4", ranksep="0.55", fontname="Noto Sans CJK SC", fontsize=17, labelloc=t];
node [shape=box, style="rounded,filled", fillcolor="#f2f6fa", color="#64748b", fontcolor="#172b3a", fontname="Noto Sans CJK SC", fontsize=15, margin="0.18,0.14", penwidth=1.1];
edge [color="#52728c", fontcolor="#334b5f", fontname="Noto Sans CJK SC", fontsize=13, arrowsize=0.7, dir=both];
'''
DIAGRAMS = {
 'topology-01-direct': '''
 graph [label="01 · 两端直连｜当前回归连接关系"];
 h [label="i.MX93 Linux Host\\n本地 EID 8 / PLDM Requester\\nLinux network 1 · mctpi3c0"];
 bus [label="I3C 总线", shape=ellipse, fillcolor="#e5f0f7"];
 t [label="Current Proj Target\\nEID 9 / PLDM TID 1\\nPID beee00000101 / DA 0x44"];
 h -> bus [label="MCTP 消息：EID 8 ↔ EID 9"];
 bus -> t [label="Private Write → IBI → Private Read"];
 ''',
 'topology-02-multi-target': '''
 graph [label="02 · 同总线多 Target｜同一 MCTP network（教学示例）"];
 h [label="Host 本地 EID 8\\nnetwork 1 · mctpi3c0"];
 bus [label="同一条 I3C 总线", shape=ellipse, fillcolor="#e5f0f7"];
 a [label="Target A\\nEID 9 / TID 1\\nPID-A / DA 0x44"];
 b [label="Target B\\nEID 10 / TID 2\\nPID-B / DA 0x45"];
 c [label="Target C\\nEID 11 / TID 3\\nPID-C / DA 0x46"];
 h -> bus; bus -> a; bus -> b; bus -> c;
 {rank=same;a;b;c;}
 ''',
 'topology-03-multi-interface': '''
 graph [label="03 · Host 多接口｜同一 network，不自动启用转发（教学示例）"];
 h [label="同一个 Linux Host / PLDM 应用\\n两条接口都属于 network 1"];
 i0 [label="mctpi3c0\\n本地 EID 8"];
 i1 [label="mctpi3c1\\n本地 EID 12"];
 a [label="I3C 总线 A", shape=ellipse, fillcolor="#e5f0f7"];
 b [label="I3C 总线 B", shape=ellipse, fillcolor="#e5f0f7"];
 ta [label="Target A\\nEID 9 / TID 1\\nPID-A / DA 0x44"];
 tb [label="Target B\\nEID 10 / TID 2\\nPID-B / DA 0x44"];
 h -> i0 [label="路由选 EID 9 的出口"]; h -> i1 [label="路由选 EID 10 的出口"];
 i0 -> a; i1 -> b; a -> ta; b -> tb;
 {rank=same;i0;i1;} {rank=same;a;b;} {rank=same;ta;tb;}
 ''',
 'topology-04-bridge': '''
 graph [label="04 · 一座 MCTP bridge｜一个 network、两段 I3C（教学示例）"];
 h [label="Host EID 8\\n最终目的 EID 20\\n下一跳：bridge B"];
 a [label="I3C 总线 A\\n本跳寻址 bridge DA 0x44", shape=ellipse, fillcolor="#e5f0f7"];
 bridge [label="MCTP bridge B\\n上行端点 EID 9 / 下行端点 EID 16\\n查目的 EID 20 → 下游出口", fillcolor="#e6eef9"];
 b [label="I3C 总线 B\\n本跳寻址 Target DA 0x30", shape=ellipse, fillcolor="#e5f0f7"];
 t [label="最终 Target\\nEID 20 / PLDM TID 1\\n在这里执行 GetTID"];
 h -> a [label="源 8 / 目的 20"];
 a -> bridge [label="Host 的邻居是 bridge PID"];
 bridge -> b [label="转发仍是源 8 / 目的 20"];
 b -> t [label="两段独立 I3C 事务，各自校验 PEC"];
 ''',
 'topology-05-multi-hop': '''
 graph [label="05 · 多级跨介质桥接｜同一 MCTP network（教学示例）"];
 h [label="Host EID 8\\n发往 EID 32 的 PLDM GetTID"];
 b [label="bridge B + 下游 Bus Owner\\n上行 EID 9 / 下行 EID 16\\n获得地址池 16～39", fillcolor="#e6eef9"];
 c [label="bridge C + 下游 Bus Owner\\n上行 EID 17 / 下行 EID 24\\n从 B 获得地址池 24～39", fillcolor="#e6eef9"];
 t [label="最终 Target EID 32\\nPLDM TID 2\\n这里解析命令并生成响应"];
 h -> b [label="I3C binding：本跳 PID / DA\\nMCTP 源 8、目的 32"];
 b -> c [label="SMBus binding：本跳链路地址\\nMCTP 源 8、目的 32"];
 c -> t [label="PCIe VDM binding：本跳路由信息\\nMCTP 源 8、目的 32"];
 ''',
 'topology-06-independent-networks': '''
 graph [label="06 · 两个独立 network｜EID 可重复，地址空间不互通（教学示例）"];
 h [label="同一个 Linux Host\\n应用必须选择 network + EID"];
 subgraph cluster_a {
  label="Linux network 1"; color="#98b0c4"; style=dashed; fontname="Noto Sans CJK SC"; fontsize=16;
  i0 [label="mctpi3c0 / 本地 EID 8"];
  a [label="I3C 总线 A", shape=ellipse, fillcolor="#e5f0f7"];
  ta [label="系统 A 的 Target\\nEID 9 / TID 1\\nDA 0x44"];
  i0 -> a; a -> ta;
 }
 subgraph cluster_b {
  label="Linux network 2"; color="#98b0c4"; style=dashed; fontname="Noto Sans CJK SC"; fontsize=16;
  i1 [label="mctpi3c1 / 本地 EID 8"];
  b [label="I3C 总线 B", shape=ellipse, fillcolor="#e5f0f7"];
  tb [label="系统 B 的 Target\\nEID 9 / TID 2\\nDA 0x44"];
  i1 -> b; b -> tb;
 }
 h -> i0 [label="(network 1, EID 9)"]; h -> i1 [label="(network 2, EID 9)"];
 {rank=same;i0;i1;} {rank=same;a;b;} {rank=same;ta;tb;}
 '''
}

if __name__ == '__main__':
 OUT.mkdir(exist_ok=True)
 for name, body in DIAGRAMS.items():
  source = ('digraph topology {\n' + STYLE + body + '\n}\n').encode()
  for fmt in ('svg', 'png'):
   # Graphviz 2.43 scales SVG geometry for dpi without enlarging its viewBox.
   # Only raster output needs a DPI override; leave SVG in native coordinates.
   command = ['dot', '-T' + fmt] + (['-Gdpi=150'] if fmt == 'png' else [])
   result = subprocess.run(command, input=source, capture_output=True, check=True)
   (OUT / (name + '.' + fmt)).write_bytes(result.stdout)
   if fmt == 'svg':
    tree = ET.fromstring(result.stdout)
    assert tree.tag.endswith('svg')
    assert any('EID' in (n.text or '') for n in tree.iter())
    graph = tree.find('{http://www.w3.org/2000/svg}g')
    assert graph is not None and graph.attrib['transform'].startswith('scale(1 1)'), 'SVG geometry must match its viewBox'
   else:
    assert result.stdout.startswith(b'\x89PNG\r\n\x1a\n')
  print(name + ': SVG / PNG generated')
