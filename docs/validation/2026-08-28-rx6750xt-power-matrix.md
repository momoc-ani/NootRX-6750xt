# RX 6750 XT 电源参数组合验证计划

## 目标与边界

本计划用于定位 RX 6750 XT 在 Tahoe 上发生 `GFX is hung`、GPU reset 失败和 WindowServer watchdog 的触发条件。测试保持 NootRX 的 Navi22、Metal、VideoToolbox 和 OpenDesign 路径不变，只调整四项已经批准的 PowerPlay 实验开关。

稳定基线为：

```text
PP_DisableULV=1
PP_GfxOffControl=0
PP_Falcon_QuickTransition_Enable=0
PP_WorkLoadPolicyMask=0
```

`Mask` 的四个位从低到高依次代表 ULV、GFXOFF、Falcon Quick Transition 和 WorkLoadPolicyMask。位为 `1` 表示该项通过 boot-arg 恢复为上游值。没有列出的实验 boot-arg 必须从 OpenCore `boot-args` 中删除，让该项使用稳定默认值。

`-NRXPowerDiag` 只启用诊断路由和日志，不改变四项 PowerPlay 值。测试期间保留原有的其他必要 boot-arg，不添加 `debug=0x100`。

## 严格执行顺序

下表是完整的四位 Gray-code 顺序，相邻两轮只改变一个参数。每轮开始前先与用户确认；未经确认不进入下一轮。

| 轮次 | Mask | ULV | GFXOFF | Falcon | WorkLoad | 本轮实验 boot-args |
|---:|---:|---:|---:|---:|---:|---|
| 0 | 0 | 1 | 0 | 0 | 0 | `-NRXPowerDiag` |
| 1 | 1 | 0 | 0 | 0 | 0 | `-NRXPowerDiag nootrx-pp-disable-ulv=0` |
| 2 | 3 | 0 | 1 | 0 | 0 | `-NRXPowerDiag nootrx-pp-disable-ulv=0 nootrx-pp-gfxoff-control=1` |
| 3 | 2 | 1 | 1 | 0 | 0 | `-NRXPowerDiag nootrx-pp-gfxoff-control=1` |
| 4 | 6 | 1 | 1 | 1 | 0 | `-NRXPowerDiag nootrx-pp-gfxoff-control=1 nootrx-pp-falcon-quick-transition=1` |
| 5 | 7 | 0 | 1 | 1 | 0 | `-NRXPowerDiag nootrx-pp-disable-ulv=0 nootrx-pp-gfxoff-control=1 nootrx-pp-falcon-quick-transition=1` |
| 6 | 5 | 0 | 0 | 1 | 0 | `-NRXPowerDiag nootrx-pp-disable-ulv=0 nootrx-pp-falcon-quick-transition=1` |
| 7 | 4 | 1 | 0 | 1 | 0 | `-NRXPowerDiag nootrx-pp-falcon-quick-transition=1` |
| 8 | 12 | 1 | 0 | 1 | 16 | `-NRXPowerDiag nootrx-pp-falcon-quick-transition=1 nootrx-pp-workload-policy-mask=16` |
| 9 | 13 | 0 | 0 | 1 | 16 | `-NRXPowerDiag nootrx-pp-disable-ulv=0 nootrx-pp-falcon-quick-transition=1 nootrx-pp-workload-policy-mask=16` |
| 10 | 15 | 0 | 1 | 1 | 16 | `-NRXPowerDiag nootrx-pp-disable-ulv=0 nootrx-pp-gfxoff-control=1 nootrx-pp-falcon-quick-transition=1 nootrx-pp-workload-policy-mask=16` |
| 11 | 14 | 1 | 1 | 1 | 16 | `-NRXPowerDiag nootrx-pp-gfxoff-control=1 nootrx-pp-falcon-quick-transition=1 nootrx-pp-workload-policy-mask=16` |
| 12 | 10 | 1 | 1 | 0 | 16 | `-NRXPowerDiag nootrx-pp-gfxoff-control=1 nootrx-pp-workload-policy-mask=16` |
| 13 | 11 | 0 | 1 | 0 | 16 | `-NRXPowerDiag nootrx-pp-disable-ulv=0 nootrx-pp-gfxoff-control=1 nootrx-pp-workload-policy-mask=16` |
| 14 | 9 | 0 | 0 | 0 | 16 | `-NRXPowerDiag nootrx-pp-disable-ulv=0 nootrx-pp-workload-policy-mask=16` |
| 15 | 8 | 1 | 0 | 0 | 16 | `-NRXPowerDiag nootrx-pp-workload-policy-mask=16` |

每轮至少观察 24 小时。Mask 15 的全开启组合至少观察 72 小时。若某轮失败，停止序列并恢复 Mask 0；该失败轮之后的组合不继续执行，直到分析日志并与用户确定下一步。

## 每轮开始检查

重启后、开始负载前执行：

```sh
nvram -p | grep boot-args
ioreg -l -w0 -p IOService | grep -E 'NootRXPowerProfileMask|NootRX_PP_'
ioreg -l -w0 -r -c IOAccelerator | grep -E 'recoveryCount|Temperature|MetalPluginName'
log show --last 10m --style compact --predicate 'eventMessage CONTAINS[c] "NootRX" AND (eventMessage CONTAINS[c] "PowerPlay profile" OR eventMessage CONTAINS[c] "PowerPlay override" OR eventMessage CONTAINS[c] "Applied RX 6750 XT")'
```

检查标准：

- `NootRXPowerProfileMask` 与表格 Mask 相同；四项 `NootRX_PP_*` 与本轮数值相同。
- 日志包含最终 profile 以及成功注入 framebuffer personality 的记录。
- `MetalPluginName` 存在，OpenDesign 可正常启动和渲染。
- 开始时记录 `recoveryCount`，用作本轮结束时的对照值。

IORegistry 中的 `NootRX_*` 字段证明 NootRX 选择并注入了对应组合；它不单独证明 Apple 驱动已经实际进入某个电源状态。电源切换和失败原因需要结合 DAL/PowerPlay 日志、`.gpuRestart` 和 panic 报告判断。

## 固定负载流程

每轮使用同一流程，避免同时改变其他变量：

1. 普通桌面和 Terminal 操作 30 分钟。
2. OpenDesign/Metal 连续渲染不少于 60 分钟，确认画面和交互无异常。
3. Chrome 或 Edge 保持硬件加速，播放视频并滚动复杂页面不少于 60 分钟。
4. 主动关闭显示器或等待显示器休眠，至少完成 3 次关闭、等待 10 分钟、重新唤醒。
5. 第二台机器持续检查 ping 和 SSH；记录中断开始和恢复时间。
6. 剩余观察期保持日常使用，不更换 kext、显卡参数、显示接口或 BIOS 设置。

## 立即停止条件

出现以下任一现象，本轮判为失败：

- 新增 `Kernel_*.gpuRestart`；
- 日志出现 `GFX is hung`、`GPU Reset failed` 或同类 reset 失败；
- WindowServer watchdog；
- 屏幕黑屏且键盘无法唤醒；
- ping 与 SSH 同时中断；
- 系统自动重启或 kernel panic。

失败后先保留证据，再恢复稳定 Mask 0。不要先删除诊断报告或清理日志。

## 故障证据收集

发生故障并重新开机后执行，其中 `XX` 替换为失败轮次：

```sh
mkdir -p /Users/momoc/Desktop/NootRX-diagnostics/round-XX
nvram -p > /Users/momoc/Desktop/NootRX-diagnostics/round-XX/nvram.txt
ioreg -l -w0 -p IOService > /Users/momoc/Desktop/NootRX-diagnostics/round-XX/ioreg.txt
log show --last 24h --style compact --predicate 'eventMessage CONTAINS[c] "NootRX" OR eventMessage CONTAINS[c] "GPU Reset" OR eventMessage CONTAINS[c] "GFX is hung" OR eventMessage CONTAINS[c] "watchdog" OR eventMessage CONTAINS[c] "channel"' > /Users/momoc/Desktop/NootRX-diagnostics/round-XX/gpu-log.txt
find /Library/Logs/DiagnosticReports -maxdepth 1 -name 'Kernel_*.gpuRestart' -exec cp -p {} /Users/momoc/Desktop/NootRX-diagnostics/round-XX/ \;
find /Library/Logs/DiagnosticReports -maxdepth 1 -name '*.panic' -exec cp -p {} /Users/momoc/Desktop/NootRX-diagnostics/round-XX/ \;
find /Library/Logs/DiagnosticReports/Retired -maxdepth 1 -name '*.panic' -exec cp -p {} /Users/momoc/Desktop/NootRX-diagnostics/round-XX/ \;
```

如果某个通配路径没有文件，`cp` 会提示找不到文件，不影响其他已收集证据。不要用修改时间覆盖旧报告；每轮使用独立目录。

## 每轮沟通记录模板

```text
轮次 / Mask：
启动时间：
实际 boot-args：
IORegistry 四项值：
开始 recoveryCount / 温度：
结束 recoveryCount / 温度：
OpenDesign / Metal：通过 / 失败
浏览器硬件加速：通过 / 失败
显示器关闭唤醒 3 次：通过 / 失败
ping / SSH：通过 / 失败
新增 gpuRestart / panic：无 / 有（文件名）
观察时长：
本轮结论：通过 / 失败 / 继续观察
备注：
```

只有用户确认本轮记录后，才准备下一轮的 boot-args。
