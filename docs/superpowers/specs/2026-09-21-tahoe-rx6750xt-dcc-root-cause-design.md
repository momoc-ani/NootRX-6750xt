# Tahoe RX 6750 XT Displayable DCC 根因定位与修复设计

## 背景

目标环境为 macOS Tahoe 26.4.1、RX 6750 XT（PCI device `0x73DF`、revision `0xC0`）与 NootRX。Chromium/Skia 在启用 GPU Rasterization 时曾出现物理画面局部闪烁，Safari 未复现；禁用 GPU Rasterization 或将 `GPUDCCDisplayable` 设为 `false` 后均未复现。

当前稳定对照配置使用：

```text
nootrx-gpu-dcc-displayable=0
```

当前实机状态已经确认：

```text
NootRX_GPUDCCDisplayableOverride = 1
NootRX_GPUDCCDisplayable = 0
GPUDCCDisplayable = No
recoveryCount = 0
```

本次启动尚未发现 `GFX is hung`、`GPU Reset failed` 或 WindowServer watchdog。该结果只证明当前短期状态正常，不能证明长期死机已解决，也不能单凭结果确定 DCC 内部哪个字段错误。

## 已确认的驱动链路

Tahoe 的 `AMDRadeonX6000` 二进制保留了可用于定位的符号。静态分析已经确认 Displayable DCC 的主要决策链路为：

```text
Chromium/Skia 创建可显示 IOSurface
    ↓
AMDRadeonX6000_AMDAccelResourceAddr2::shouldAllocScanoutDcc(...)
    ↓
检查全局 GPUDCCDisplayable 状态
    ↓
检查资源是否具有 displayable 标志
    ↓
通过 sendRequestToController(..., requestType=0x1A, ...) 查询 Framebuffer
    ↓
AMDRadeonX6000_AMDHWAlignManager2::getDccInfo2(...) 计算 DCC metadata
    ↓
Framebuffer/DAL 接收 surface 与 DCC 参数并执行 scanout
```

`shouldAllocScanoutDcc` 的反汇编还表明：只有全局 DCC 开关、surface 条件和 Framebuffer 返回值同时满足时，驱动才为可显示 surface 分配 DCC。因此 `GPUDCCDisplayable=false` 是绕过整条路径的稳定规避，不是底层修复。

当前可能发生不一致的边界包括：

- Navi23 donor capability 对实际 Navi22 硬件的描述不完整；
- AddrLib2 接收到的 ASIC family、revision 或 swizzle 条件不正确；
- GFX 侧生成的 metadata pitch、大小、对齐或 block geometry 与显示侧要求不一致；
- request type `0x1A` 的输入或 Framebuffer 返回能力与实际 surface 不匹配；
- Framebuffer/DAL 对 metadata 的解释与加速器生成结果不一致。

以上均为待验证假设，不能在取得输入与输出证据前直接选择其中一个进行修补。

## 目标优先级

### 第一优先级：修复完整 Displayable DCC 路径

- 删除对 `nootrx-gpu-dcc-displayable=0` 的依赖；
- Tahoe + RX 6750 XT 默认保持 Displayable DCC 开启；
- 修复已经定位的 capability、ASIC 标识、metadata 或 scanout 参数；
- 保留 Metal、OpenDesign、VideoToolbox 和 Chromium GPU Rasterization。

### 第二优先级：按 surface 选择性降级

如果证据证明只有特定格式、swizzle mode 或 IOSurface 组合不兼容，则只拒绝这些 surface 使用 Displayable DCC。普通纹理、离屏资源和其他兼容的显示 surface 继续使用 DCC。

### 最后保底：目标硬件自动关闭 Displayable DCC

仅当 Tahoe 的 Apple 驱动缺少 RX 6750 XT 所需的底层能力，且无法通过范围明确的局部补丁修复时，才针对 Tahoe + `0x73DF/0xC0` 自动关闭 Displayable DCC。该方案移除 boot-arg 依赖，但功能上仍属于兼容性 quirk。

## 非目标

- 不同时调整 ULV、GFXOFF、Falcon Quick Transition 或 WorkLoadPolicy；
- 不把 Chromium 启动参数作为最终修复；
- 不在没有证据的情况下更换 Navi22/Navi23 全套 donor；
- 不修改 Metal、OpenDesign 或 VideoToolbox 的启用状态；
- 不把本次闪烁结论直接等同于长期 GPU hang 的根因；
- 不在诊断阶段加入多个互相影响的修复。

## 设计方案

### 阶段一：静态结构还原

先还原下列函数的参数、返回值和相关结构，期间不修改 EFI，也不改变运行时行为：

- `AMDRadeonX6000_AMDAccelResourceAddr2::shouldAllocScanoutDcc`；
- `AMDRadeonX6000_AMDHWAlignManager2::getDccInfo2`；
- accelerator 到 Framebuffer 的 request type `0x1A` 数据结构；
- `AmdDalServices::getDccCapabilities`；
- `AmdDalHelper::prepareDalDisplaySurfaceParameters`；
- `AmdDalHelper::updateSurfaceInfo`；
- `AmdAgdcServices::getScanoutResourceConfig` 及相关 entry 方法。

静态分析需要回答以下问题：

1. `GPUDCCDisplayable` 最终保存在哪个 accelerator 状态字段；
2. `shouldAllocScanoutDcc` 的四个整数参数分别表示什么；
3. request type `0x1A` 的输入、输出结构及成功条件；
4. AddrLib2 的 ASIC family、revision、swizzle mode 从哪里取得；
5. metadata pitch、height、大小、对齐和 block size 在 accelerator 与 Framebuffer 之间如何传递。

### 阶段二：独立诊断模块

新增一个职责单一的 DCC 诊断模块，负责格式化、限频和发布诊断快照。路由仍分别放在对应组件：

- accelerator 与 AddrLib2 路径放在 `X6000` 相关模块；
- Framebuffer/DAL 路径放在 `X6000FB` 模块；
- 公共快照结构与输出逻辑独立封装，避免把诊断状态散落在现有补丁代码中。

所有新增方法都必须添加用途注释；关键的数据转换和驱动边界必须添加简短注释。

诊断功能只在以下条件同时成立时启用：

```text
macOS Tahoe build 25E253
device-id = 0x73DF
pci-revision = 0xC0
-NRXPowerDiag 已启用
```

不增加新的诊断 boot-arg。其他 Tahoe build 必须重新核对四个符号、函数体、虚表偏移和 ABI 后才能扩展门控。未启用 `-NRXPowerDiag` 时，不路由诊断函数，也不产生额外的逐 surface 日志。

### 诊断数据

每次关键 DCC 决策至少记录：

- 单调递增的诊断序号；
- surface 宽度、高度、像素格式和资源标志；
- displayable 判定；
- AddrLib2 使用的 ASIC family、revision 与 swizzle mode；
- `shouldAllocScanoutDcc` 的判定结果；
- request type `0x1A` 的输入、返回码和 capability 结果；
- metadata pitch、height、总大小和对齐；
- independent block size 与相关 DCC capability 位；
- Framebuffer 最终采用或拒绝 DCC 的结果。

禁止为每帧无条件打印日志。采用以下限频规则：

- 首次看到一种新的 surface 参数组合时记录；
- capability、metadata 或最终判定发生变化时记录；
- 返回失败、字段越界或 accelerator/Framebuffer 不一致时立即记录；
- 重复且一致的结果只增加计数器。

同时在 GPU 的 IORegistry 节点发布最近一次快照和累计计数，便于在系统仍可响应时直接读取。IORegistry 数据不跨重启持久化；跨重启调查以统一日志和已有崩溃报告为准。

### 阶段三：一次受控复现

诊断驱动通过构建和静态守卫验证后，才进行一次 DCC 开启复现：

1. 保留当前可回滚的 EFI 和稳定驱动备份；
2. 仅移除 `nootrx-gpu-dcc-displayable=0`，其他电源参数不变；
3. 使用相同 Chrome 版本、默认 GPU Rasterization 和相同页面复现；
4. 收集 accelerator、AddrLib2、request `0x1A` 与 Framebuffer 四个边界的数据；
5. 出现闪烁、GFX hang、GPU reset 或 WindowServer watchdog 后立即回到稳定基线。

本阶段只改变 DCC 是否开启这一个变量，不同时测试其他参数组合。

### 阶段四：证据驱动的单点修复

根据复现数据选择且只选择一个根因位置实施补丁：

- donor capability 错误：修补具体 capability 位或使用正确表项；
- ASIC 标识错误：只修正传给 AddrLib2 的 family/revision；
- metadata 计算错误：修正已经证实错误的 pitch、alignment、size 或 block 参数；
- scanout 判定错误：修正 request `0x1A` 或 Framebuffer capability 转换；
- 特定 surface 不兼容：在 `shouldAllocScanoutDcc` 边界加入精确的 surface 筛选。

不得把多个候选修复同时打入同一个测试版本。若某次修复未通过验证，应保留诊断结果并回到根因分析，不在失败补丁上继续叠加猜测性修改。

## 降级决策条件

只有满足以下证据条件后，才允许从完整 DCC 修复降级到选择性禁用：

- 不兼容可以稳定归纳为明确的像素格式、surface 标志、swizzle mode 或尺寸约束；
- 其他 surface 的 accelerator 与 Framebuffer 参数保持一致；
- 精确筛选不会关闭普通纹理或离屏资源的 DCC。

只有满足以下条件后，才允许自动关闭全部 Displayable DCC：

- 所有 displayable surface 均可能产生不可修正的不一致；或
- Tahoe 驱动缺少 Navi22 所需实现，局部修补必须演变成大范围重写 Apple 驱动；或
- 已完成三次彼此独立、均有证据支持的定点修复，但仍在不同边界出现不可恢复的错误，需要重新评估整体兼容架构。

## 验证方案

### 静态与单元验证

- 诊断路由仅对 Tahoe + `0x73DF/0xC0` + `-NRXPowerDiag` 生效；
- 未启用诊断时原有控制流和 personality 内容不变；
- 限频器对重复事件只累计，不连续刷日志；
- 快照字段能够区分 capability、metadata 和 Framebuffer 最终判定；
- 现有 PowerProfile、DDI capability、GC policy 和 DCC policy 测试继续通过。

### 构建验证

- Release x86_64 构建成功；
- 新增 route 在 Tahoe 当前二进制中可以唯一定位；
- route 或结构校验失败时安全停止该诊断分支，不修改 DCC 行为。

### 实机修复验收

- 删除 `nootrx-gpu-dcc-displayable` 后 IORegistry 显示 `GPUDCCDisplayable=Yes`；
- Chrome 使用默认 GPU Rasterization，不增加浏览器启动参数；
- 原复现页面和视频不再闪烁；
- Safari、Metal、OpenDesign 和 VideoToolbox 正常；
- `recoveryCount` 不增长；
- 不出现 `GFX is hung`、GPU Reset 或 WindowServer watchdog；
- 长期死机是否解决作为独立的持续运行结果记录，不用短期闪烁验证替代。

## 回滚

- 诊断版本出现异常时，恢复当前已验证的 NootRX.kext；
- DCC 开启复现出现异常时，恢复 `nootrx-gpu-dcc-displayable=0`；
- 单点修复验证失败时，仅回滚该修复，保留已验证的诊断设施；
- 不修改或覆盖用户当前未提交的固件和问题记录文件。

## 预期结果

本方案不会把“关闭 DCC 后不闪”直接包装成驱动修复。它先确定 accelerator 生成的 DCC 参数与 Framebuffer 使用的参数在哪个边界开始不一致，再实施最小定点补丁。成功时最终配置不依赖 DCC boot-arg，并保持 Displayable DCC；无法完整修复时，依次降级为特定 surface 禁用和目标硬件自动关闭。
