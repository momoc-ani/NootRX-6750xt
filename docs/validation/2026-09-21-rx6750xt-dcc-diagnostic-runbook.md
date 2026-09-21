# Tahoe RX 6750 XT Displayable DCC 诊断运行手册

## 目的与边界

本手册用于 macOS Tahoe 26.4.1 build `25E253`、RX 6750 XT `0x73DF/0xC0`。目标是在不猜测修改 capability、ASIC identity 或 metadata 的前提下，只进行一次 Displayable DCC 开启复现，定位 accelerator、AddrLib2 与 Framebuffer 之间第一个发生不一致的边界。

诊断候选版必须保留 Metal、OpenDesign、VideoToolbox、GPU compositing 与硬件视频解码。除 DCC 变量外，不更改 ULV、GFXOFF、Falcon Quick Transition、WorkLoadPolicy、Chrome 参数、显示器连接和页面复现条件。

## 阶段一：DCC 关闭基线

### 启动条件

OpenCore boot-args 保持：

```text
-NRXPowerDiag
nootrx-gpu-dcc-displayable=0
```

先安装诊断 kext，但不改变当前 DCC 关闭值。重启后必须确认：

```text
NootRX_DCCDiagEnabled = 1
NootRX_GPUDCCDisplayableOverride = 1
NootRX_GPUDCCDisplayable = 0
GPUDCCDisplayable = No
recoveryCount = 0
```

采集当前快照：

```sh
ioreg -lw0 -p IOService | rg 'NootRX_DCCDiag|GPUDCCDisplayable|recoveryCount'
log show --last boot --style compact --predicate 'eventMessage CONTAINS[c] "DCCDIAG" OR eventMessage CONTAINS[c] "GFX is hung" OR eventMessage CONTAINS[c] "GPU Reset" OR eventMessage CONTAINS[c] "watchdog"'
ls -lt /Library/Logs/DiagnosticReports/Kernel_*.gpuRestart /Library/Logs/DiagnosticReports/Retired/Kernel-*.panic 2>/dev/null
```

基线通过条件：

- identity 记录的 engine、family、revision 稳定；
- 没有 `addrlib-abi` 或 `framebuffer-abi`；
- 没有 route failure、`GFX is hung`、GPU reset 或 WindowServer watchdog；
- PowerPlay 四项与当前稳定配置一致。

任一条件不成立时，不进入阶段二；恢复上一版 kext，并保留日志。

## 阶段二：一次 DCC 开启复现

只有用户明确确认后才执行。先备份 `config.plist`，并验证 DCC boot-arg 在配置中恰好出现一次。然后只把：

```text
nootrx-gpu-dcc-displayable=0
```

改为明确的：

```text
nootrx-gpu-dcc-displayable=1
```

这样可以避免“删除参数后由其他配置来源接管”的歧义。不得同时修改其他 boot-args 或浏览器参数。

重启后先确认：

```text
GPUDCCDisplayable = Yes
NootRX_GPUDCCDisplayable = 1
NootRX_DCCDiagEnabled = 1
```

然后使用与基线相同的 Chrome 版本、默认 GPU Rasterization、相同页面、窗口尺寸和显示器连接进行复现。出现以下任一现象即停止继续操作并采集：

- 页面彩色矩形或闪烁；
- 鼠标可移动但界面停止刷新；
- `GFX is hung`；
- GPU reset；
- WindowServer watchdog；
- 系统自动重启。

系统仍可响应时立即执行：

```sh
ioreg -lw0 -p IOService | rg 'NootRX_DCCDiag|GPUDCCDisplayable|recoveryCount'
log show --last boot --style compact --predicate 'eventMessage CONTAINS[c] "DCCDIAG" OR eventMessage CONTAINS[c] "GFX is hung" OR eventMessage CONTAINS[c] "GPU Reset" OR eventMessage CONTAINS[c] "watchdog"'
ls -lt /Library/Logs/DiagnosticReports/Kernel_*.gpuRestart /Library/Logs/DiagnosticReports/Retired/Kernel-*.panic 2>/dev/null
```

如果系统自动重启，先恢复稳定 DCC 配置，再按故障时间窗口查询持久化日志：

```sh
log show --last 2h --style compact --predicate 'eventMessage CONTAINS[c] "DCCDIAG" OR eventMessage CONTAINS[c] "GFX is hung" OR eventMessage CONTAINS[c] "GPU Reset" OR eventMessage CONTAINS[c] "watchdog"'
ls -lt /Library/Logs/DiagnosticReports/Kernel_*.gpuRestart /Library/Logs/DiagnosticReports/Retired/Kernel-*.panic 2>/dev/null
```

## 恢复稳定基线

采集完成后，把 boot-arg 恢复为：

```text
nootrx-gpu-dcc-displayable=0
```

并重启确认 `GPUDCCDisplayable=No`。诊断实验配置不得长期保留在 DCC 开启状态。

## 证据归类

按 `NootRX_DCCDiagSequence` 对齐 `identity`、`scanout`、`framebuffer`、`addrlib` 四类记录，只选择第一个出现不一致的边界：

| 首个异常边界 | 后续唯一修复方向 |
| --- | --- |
| `identity` family/revision 与 Navi22 预期不符 | 设计 AddrLib ASIC identity 定点修复 |
| Framebuffer capability 成功，但 `addrlib` 返回失败或 metadata 为零 | 设计 AddrLib 输入或 metadata 定点修复 |
| AddrLib 输出稳定，但同一宽高/格式的 Framebuffer capability 变化 | 设计 request `0x1A` capability 转换修复 |
| 只有特定 pixel format、candidateFlags 或 swizzle 组合失败 | 设计精确 surface 筛选，保留其他 Displayable DCC |
| 四个边界均一致，但显示仍异常 | 停止修改，增加下游 DAL/scanout 边界诊断，不猜测改值 |
| 所有 displayable 组合均无法兼容且局部路径不可修复 | 最后才评估 Tahoe + `0x73DF/0xC0` 自动关闭 Displayable DCC |

在完成归类并取得用户批准前，不修改 capability 位、ASIC family/revision、metadata、scanout 返回值或默认 DCC 策略。
