# Tahoe RX 6750 XT DCC 两项功能测试计划与进度

## 测试目标

本计划只验证以下两个已经实现的功能：

1. 低开销、原子发布的 DCC observation recorder；
2. Tahoe `25E253` 安全路由、显式 ABI 与失败关闭。

独立 `-NRXDCCDiag` 是测试隔离条件，用于防止 `-NRXPowerDiag` 的全量 PowerPlay/DAL 日志干扰卡顿判断。Displayable DCC `=1` 不属于本轮两项功能的通过条件。

## 固定测试环境

```text
macOS: 26.4.1 (25E253)
GPU: RX 6750 XT 0x73DF/0xC0
SMBIOS: MacPro7,1
Displayable DCC baseline: nootrx-gpu-dcc-displayable=0
Power profile: ULV=1, GFXOFF=0, FalconQuick=0, WorkLoadPolicyMask=0
```

计划部署后的诊断参数：

```text
-NRXDCCDiag nootrx-gpu-dcc-displayable=0
```

本轮不得保留 `-NRXPowerDiag`，否则全量 DAL 日志会污染低开销验证。

## 已知基线证据

| 时间 | 证据 | 结论 |
| --- | --- | --- |
| 2026-09-21 17:39:19 | `/Library/Logs/DiagnosticReports/Kernel_2026-09-21-173919_Mac-Pro.gpuRestart` | DCC 关闭时仍发生 GPU Reset |
| 2026-09-21 17:39:19 | 内核日志 `channel 28 GFX is hung` | 主 GFX 队列发生硬件超时 |
| 2026-09-21 17:39:19 | channel 34 FirstPendingCB = WindowServer | 画面冻结与同一次 GFX hang 一致 |
| 2026-09-21 17:39:24 | `Stamp Timeout for KIQ Submission` | reset 阶段 KIQ 也未正常推进 |
| 故障前后约 90 秒 | 40,348 行 AMDRadeon 内核日志 | `-NRXPowerDiag` 不适合作为长期低开销诊断开关 |

这组证据说明：DCC 可能仍与 Chromium 闪烁有关，但不是 GPU Reset 的唯一原因。

## 状态定义

- `已完成`：有本轮新鲜命令输出或持久化报告支持；
- `等待重启`：代码、构建和 EFI 已验证，但尚未加载；
- `等待部署`：代码与构建已验证，但尚未替换实际启动 EFI；
- `实机观察中`：启动条件通过，等待正常使用反馈；
- `失败`：通过条件不满足，停止后续阶段；
- `未开始`：前置条件尚未满足。

## 进度总表

| 阶段 | 验证内容 | 当前状态 | 证据/结果 |
| --- | --- | --- | --- |
| P0 | 旧版卡顿与 GPU Reset 取证 | 已完成 | 17:39 报告与内核日志已读取 |
| P1 | 独立诊断模式红绿测试 | 已完成 | 四种参数组合测试退出码为 0；Power 与 DCC 状态独立 |
| P2 | 两项功能完整主机回归 | 已完成 | 6 个 C++ 测试与 5 个 shell 集成守卫全部退出码为 0 |
| P3 | clean Release 构建与产物校验 | 已完成 | `BUILD SUCCEEDED`；x86_64 kext、plist 与诊断标记校验通过 |
| P4 | 远程推送与 EFI 落盘校验 | 已完成 | 分支已推送；EFI kext 逐文件一致，boot-args 精确计数通过 |
| P5 | 重启后的安全路由成功路径 | 失败 | 新 kext 已加载，但 target gate 返回 false：`Enabled=0`、`RouteMask=0`、`FailureCode=0` |
| P5.1 | 剩余门控输入观测版 | 失败 | `Requested=1`、`OSBuildMatch=0`；早期 `osversion` 精确 build gate 未通过 |
| P5.2 | IORegistry build gate 修正版 | 等待部署 | 6 个 C++ 测试、5 个 shell 守卫、clean Release 构建与产物校验均通过 |
| P6 | 重启后的低开销运行观察 | 未开始 | P5 未通过，不进入运行观察 |
| P7 | Displayable DCC `=1` 根因实验 | 未开始 | 不属于本轮通过条件，需用户再次确认 |

## P1：独立诊断模式

自动化测试四种组合：

| Power 参数 | DCC 参数 | 预期 Power 全量日志 | 预期 DCC recorder |
| --- | --- | --- | --- |
| 关闭 | 关闭 | 关闭 | 关闭 |
| 关闭 | 开启 | 关闭 | 开启 |
| 开启 | 关闭 | 开启 | 关闭 |
| 开启 | 开启 | 开启 | 开启 |

通过条件：`-NRXDCCDiag` 不会启用 PowerPlay/DAL 全量日志；`-NRXPowerDiag` 不会隐式启用 DCC recorder。

## P2：两项功能主机验证

功能一通过条件：

- 四阶段容量互不影响；
- 成功重复只在 `1、2、4、8...` publish；
- 失败重复只在相同检查点 publish+log；
- 非检查点重复不 publish、不 log；
- 不再出现旧的 `NootRX_DCCDiagLast*` 多属性发布；
- Framebuffer float 尺寸使用原始位记录。

功能二通过条件：

- 四个 `25E253` validator 接受真实 fixture；
- 翻转任一关键字节后拒绝；
- accelerator 符号先预解析、后一次批量 route；
- Framebuffer request `0x1A` 入口、分支和跳表均校验；
- 诊断 symbol/signature/route 失败路径调用 `disable`，不调用 panic；
- 四个 wrapper 保持 call-record-return 顺序。

## P3/P4：构建与部署

通过条件：

- 所有主机测试和 `Tests/*Tests.sh` 退出码为 0；
- clean Release x86_64 输出 `BUILD SUCCEEDED`；
- kext `Info.plist` 为 OK；
- 构建二进制包含 `-NRXDCCDiag`、snapshot、route mask 与 failure code 标记；
- EFI kext 与构建产物逐文件一致；
- EFI boot-args 中 `-NRXDCCDiag` 恰好一次、`-NRXPowerDiag` 为零次、DCC `=0` 恰好一次。

## P5：重启后安全路由

必须同时满足：

```text
NootRX_DCCDiagEnabled = 1
NootRX_DCCDiagRouteMask = 3
NootRX_DCCDiagFailureCode = 0
```

任一不满足即标记失败，不进入 P6。实机不故意修改 Apple 驱动来制造失败；失败关闭由 P2 自动化验证覆盖。

## P6：低开销正常使用观察

启动条件通过后保持 DCC `=0` 正常使用。每次出现画面卡顿、应用假死、GPU Reset 或系统重启时，记录：

- 准确时间；
- 前台应用；
- `NootRX_DCCDiagSnapshot`；
- `DCCDIAG` 日志；
- GPU Reset、GFX hang、KIQ 和 WindowServer 日志；
- 新的 `Kernel_*.gpuRestart` 或 panic 报告。

检查期间不得出现 NootRX 的 `Power diagnostics enabled by -NRXPowerDiag` 或 `Enabled AMDRadeonX6000 PowerPlay/DAL diagnostic logging`。GPU Reset 若再次发生，不自动判定这两个诊断功能失败；必须结合 route 状态和日志开销判断，同时作为主 GFX hang 的新证据继续分析。

## P7：DCC 根因实验边界

只有 P5 通过、P6 的诊断开销正常后，才讨论把：

```text
nootrx-gpu-dcc-displayable=0
```

单独改为：

```text
nootrx-gpu-dcc-displayable=1
```

该阶段用于 Chromium 闪烁与 DCC metadata/capability 链路定位，不用于证明所有 GPU Reset 都由 DCC 引起。

## 进度更新记录

| 更新时间 | 阶段 | 更新 |
| --- | --- | --- |
| 2026-09-21 | P0 | 保存 17:39 GPU Reset、channel 28 hang、channel 34 WindowServer 和 KIQ timeout 结论 |
| 2026-09-21 | 计划建立 | 建立 P1-P7 状态与停止条件 |
| 2026-09-21 18:20 +0800 | P1 | `DiagnosticsModePolicyTests` 以 `-Wall -Wextra -Werror` 编译并运行成功，四种参数组合退出码为 0 |
| 2026-09-21 18:22 +0800 | P2 | 6 个 `Tests/*Tests.cpp` 与 5 个 `Tests/*Tests.sh` 全部运行成功，覆盖 recorder、路由、诊断隔离、DCC displayable、DDI 与 Power profile |
| 2026-09-21 18:23 +0800 | P3 | clean Release x86_64 构建成功；`Info.plist` 为 OK；二进制集成守卫与 7 个必需标记校验通过 |
| 2026-09-21 18:26 +0800 | P4 | 分支推送到 `origin/dcc-root-cause-diagnostics`；EFI kext 与构建目录逐文件一致；`-NRXDCCDiag=1`、`-NRXPowerDiag=0`、DCC `=0` 恰好一次；配置与 kext plist 均为 OK |
| 2026-09-21 19:36 +0800 | P5 | 当前加载 UUID `C7370FEB-19B6-3505-9BC7-278869A26F6D` 与部署构建一致；NVRAM 参数正确，但运行态为 `Enabled=0`、`RouteMask=0`、`FailureCode=0`，按停止条件不进入 P6 |

P4 可恢复备份：

```text
/Volumes/NO NAME/EFI/OC/Kexts/NootRX.kext.backup-20260921-182454
/Volumes/NO NAME/EFI/OC/config.plist.backup-20260921-182454
```

## P5 失败定位

本次启动确认：

```text
macOS build = 25E253
GPU device-id = 0x73DF
GPU revision-id = 0xC0
boot-args contains -NRXDCCDiag
NootRX_GPUDCCDisplayableOverride = 1
NootRX_GPUDCCDisplayable = 0
recoveryCount = 0
```

`NootRX_GPUDCCDisplayableOverride=1` 由同一 Tahoe、device ID 与 PCI revision
目标条件控制，因此这三个条件已经通过。`FailureCode=0` 且 `RouteMask=0`
说明诊断没有进入锁分配、符号、签名或路由阶段，而是在 target gate 阶段返回
false。现有属性只能把剩余断点缩小到以下两个输入，不能继续区分：

- 内核启动时 `checkKernelArgument("-NRXDCCDiag")` 的结果；
- 内核全局 `osversion` 与 `25E253` 的精确比较结果。

在增加这两个门控输入的独立 IORegistry 观测前，不修改 build gate、不绕过
target gate，也不进入 Displayable DCC `=1` 实验。

## P5.1 门控输入观测版

经用户批准，只增加以下只读属性：

```text
NootRX_DCCDiagRequested
NootRX_DCCDiagOSBuildMatch
```

实现继续复用 `DCCDiagnosticsPolicy` 的精确 build 比较，`isTarget` 的五项条件和
返回结果保持不变。TDD 红灯证据为：策略测试因缺少 `observeGate` 编译失败，集成
守卫因 request 属性缺失失败。最小实现后的验证结果：

- 6 个 C++ 主机测试退出码为 0；
- 5 个 shell 集成守卫退出码为 0；
- clean Release x86_64 输出 `BUILD SUCCEEDED`；
- `Info.plist` 为 OK；
- 构建二进制 UUID 为 `2A01CC93-6A5C-3883-8997-B432D6A5D4BF`；
- 两个新增属性标记均存在于最终二进制。

| 更新时间 | 阶段 | 更新 |
| --- | --- | --- |
| 2026-09-22 09:39 +0800 | P5.1 | 门控输入观测版完成红绿测试、完整回归和 Release 构建，等待写入实际启动 EFI |
| 2026-09-22 09:46 +0800 | P5.1 | `disk1s1` 实际启动 EFI 已替换为 UUID `2A01CC93-6A5C-3883-8997-B432D6A5D4BF`；kext 逐文件一致，boot-args 未改变，等待重启 |
| 2026-09-22 10:00 +0800 | P5.1 | 已加载 UUID 与部署构建一致；`Requested=1`、`OSBuildMatch=0`、`Enabled=0`、`RouteMask=0`、`FailureCode=0`，确认 build gate 在路由前拒绝诊断 |
| 2026-09-22 09:58:57 +0800 | GPU Reset | 新报告确认 channel 31 GFX hang；首个待处理命令来自 Google Chrome Helper 的 Metal/Dawn shader；随后出现 KIQ submission timeout |

P5.1 可恢复备份：

```text
/Volumes/NO NAME/EFI/OC/Kexts/NootRX.kext.backup-20260922-094532-gate-observation
```

## P5.2 IORegistry build gate 修正版

经用户批准，DCC target gate 的 build 数据源改为：

```text
数据源 = IORegistry root / OS Build Version
缺失策略 = fail closed
global osversion fallback = none
```

`processPatcher()` 只读取一次根节点 build，并把同一个值同时传给
`DCCDiagnosticsPolicy::observeGate()` 和 `isTarget()`。Tahoe、device ID、PCI
revision、`-NRXDCCDiag`、符号和指令签名门控保持不变。请求诊断但属性缺失或不匹配
时只记录拒绝原因，仍保持 `Enabled=0`。

TDD 红灯为：

```text
FAIL: DCC diagnostics do not read the IORegistry root
```

加入最小实现后，`DCCDiagnosticsPolicyTests` 与
`DCCDiagnosticsIntegrationTests.sh` 均退出 0。实际启动通过条件仍为：

```text
NootRX_DCCDiagRequested = 1
NootRX_DCCDiagOSBuildMatch = 1
NootRX_DCCDiagEnabled = 1
NootRX_DCCDiagRouteMask = 3
NootRX_DCCDiagFailureCode = 0
```

任一条件不满足都停止复现，不开启 Displayable DCC。

完整验证结果：

- 6 个 C++ 主机测试全部编译并退出 0；
- 5 个 shell 集成守卫全部退出 0；
- clean Release x86_64 输出 `BUILD SUCCEEDED`；
- `Info.plist` 校验为 OK；
- 最终二进制包含 `OS Build Version`、build match、enabled 与 `DCCDIAG` 标记；
- 构建 UUID 为 `24434FEA-33DA-3208-B251-F8D0A69C8542`。

| 更新时间 | 阶段 | 更新 |
| --- | --- | --- |
| 2026-09-22 10:57 +0800 | P5.2 | IORegistry build gate 完成红绿测试、完整回归和 Release 构建，等待用户确认后部署 EFI |

## 2026-09-22 GPU Reset 证据

报告：

```text
/Library/Logs/DiagnosticReports/Kernel_2026-09-22-095857_Mac-Pro.gpuRestart
```

关键事实：

```text
Application = Google Chrome Helper 153.0.8010.53
Restart Channel = 31 GFX
FirstPendingCB = Google Chrome Helper
SubmitContext = Metal
Shader entry points = dawn_entry_point
GFX CompletedTS = 0x5849
GFX SubmittedTS = 0x5861
Command Processor = BUSY
Unified Shader / Texture / Render Backend = IDLE
Display HUBP dcc_en = 0
Stamp Timeout for KIQ Submission
```

本次故障发生于启动后约 7 分 28 秒。Displayable DCC 明确关闭，扫描面
`dcc_en=0`，因此本次 reset 不能归因于 Displayable DCC。首个未完成命令缓冲
及其全部 shader 标记均指向 Chromium Dawn/Metal 路径；同时 command processor
忙而 shader、texture 和 render backend 空闲，故障边界位于 GFX 命令处理/调度
路径，而不是正在执行的 shader 或显示 DCC metadata。其后 KIQ submission
timeout 说明 reset/恢复路径也没有正常推进。

本次 `NootRX_DCCDiagRequested=1`，但 `NootRX_DCCDiagOSBuildMatch=0`，所以 DCC
recorder 未启用，未生成 `NootRX_DCCDiagSnapshot`。运行后的 IORegistry 根节点存在
`OS Build Version = 25E253`；这证明系统 build 正确，但不能证明该属性在 NootRX
早期 patcher 阶段已经可用。下一步 build gate 来源必须经用户批准后再修改。
