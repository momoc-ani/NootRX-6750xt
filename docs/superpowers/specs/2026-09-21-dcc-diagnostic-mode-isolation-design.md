# DCC 诊断模式隔离设计

## 背景与证据

2026-09-21 17:39:19，在当前运行中的旧版 NootRX 上发生了真实 GPU Reset。系统当时满足：

- `GPUDCCDisplayable=No`；
- `PP_DisableULV=1`；
- `PP_GfxOffControl=0`；
- `PP_Falcon_QuickTransition_Enable=0`；
- `PP_WorkLoadPolicyMask=0`。

内核记录了 `channel 28 GFX is hung`、WindowServer 的 channel 34 待处理命令以及 `Stamp Timeout for KIQ Submission`。因此 Displayable DCC 不是 GPU Reset 的唯一触发条件。

同一故障窗口内，`-NRXPowerDiag` 开启的全量 PowerPlay/DAL 调试路径在约 90 秒内产生 40,348 行 AMDRadeon 内核日志。现有 DCC 对数限频只约束 `DCCDiagnostics` recorder，不能约束 Apple DAL 全量日志，因此二者必须解耦后才能验证低开销诊断本身。

## 已批准的行为

增加独立 boot-arg `-NRXDCCDiag`：

- `-NRXDCCDiag` 只请求 Tahoe `25E253`、RX 6750 XT `0x73DF/0xC0` 的 DCC recorder 与安全路由；
- `-NRXPowerDiag` 只保留现有 PowerPlay、GPU panic 与 DAL 全量故障抓取路径；
- 两个参数可以独立使用，也可以在短时故障抓取时同时使用；
- 稳定验证基线使用 `-NRXDCCDiag nootrx-gpu-dcc-displayable=0`，不保留 `-NRXPowerDiag`；
- 不修改 ULV、GFXOFF、Falcon Quick Transition、WorkLoadPolicy、Metal、OpenDesign、VideoToolbox 或 DCC 默认策略。

## 代码边界

创建一个无内核依赖的诊断模式策略，只负责把两个独立请求映射为两个独立状态。`NootRXMain` 负责读取 boot-arg，并使用策略结果配置现有成员：

- `powerDiagnostics` 控制全量 PowerPlay/DAL 路由和日志属性；
- `dccDiagnosticsRequested` 控制 `DCCDiagnosticsPolicy::isTarget` 的最后一个条件。

现有 DCC recorder、route validator、PowerPlay profile 和 DCC displayable policy 不改变职责。

## 本轮验证的两个功能

### 功能一：低开销原子 DCC 观察

- 四阶段各 32 个固定槽位；
- 新观察发布并记录一次；
- 重复成功只在 repeat `1、2、4、8...` 发布，不记录日志；
- 重复失败只在相同检查点发布并记录日志；
- 非检查点重复不分配、不写 IORegistry、不写日志；
- `NootRX_DCCDiagSnapshot` 作为一个字典原子替换。

### 功能二：25E253 安全路由与失败关闭

- 安装 wrapper 前预解析全部目标符号；
- 校验本机 `25E253` 关键指令和 request `0x1A` 跳表；
- accelerator 使用一次批量 route；
- 符号、签名或 route 失败只关闭 DCC 诊断，不 panic；
- 四个 wrapper 各调用 Apple 原函数一次，并原样返回。

## 实机验证边界

若 target gate 在进入锁、符号、签名或路由阶段前返回 false，GPU 节点额外发布两个
只读输入观测：

- `NootRX_DCCDiagRequested`：内核对 `-NRXDCCDiag` 的解析结果；
- `NootRX_DCCDiagOSBuildMatch`：内核 `osversion` 是否精确等于 `25E253`。

这两个属性只暴露现有输入，不参与修改门控结果，也不改变任何显卡路径。

实机不通过篡改 Apple 驱动制造签名失败。失败关闭由主机测试翻转关键指令字节、集成守卫检查无诊断 `PANIC_COND` 来验证；实机只验证成功路径：

```text
NootRX_DCCDiagEnabled = 1
NootRX_DCCDiagRouteMask = 3
NootRX_DCCDiagFailureCode = 0
```

如果 `Enabled=0`、route mask 不为 `3` 或 failure code 不为 `0`，停止实机观察并回到代码分析。

Displayable DCC 开启是单独的根因实验。只有上述两个功能在 `GPUDCCDisplayable=No` 的基线通过后，才由用户确认是否把 `nootrx-gpu-dcc-displayable=0` 改为 `1`。
