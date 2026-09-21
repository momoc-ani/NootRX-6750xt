# Tahoe DCC 诊断低开销与安全路由设计

## 背景

现有 DCC 诊断 wrapper 在 macOS Tahoe build `25E253`、RX 6750 XT `0x73DF/0xC0` 上保持只读，但代码审查确认诊断服务本身存在四类干扰风险：重复失败可能持续刷日志、重复成功仍在热路径频繁写 IORegistry、多个线程可能交错发布快照、四个阶段共享的 32 个观察槽可能过早耗尽。诊断路由失败还会触发 panic，与“诊断失败不得影响正常 DCC 行为”的目标冲突。

本设计只降低诊断开销并加强证据可靠性，不修改 Displayable DCC、PowerPlay、Metal、OpenDesign 或 VideoToolbox 行为。

## 已批准目标

- 保留四个 Apple 原函数只调用一次、输入输出不改写、返回值原样返回的约束。
- 新事件立即记录；完全相同的重复事件使用对数级检查点。
- IORegistry 使用一个完整字典原子替换，禁止逐字段交错更新。
- 四个诊断阶段各自拥有固定容量，避免互相挤占。
- 安装 route 前验证 build、符号和关键指令；失败时关闭诊断，不触发 panic。
- 补全 AddrLib 输入、metadata、block geometry 和无地址的指针存在性字段。

## 限频策略

每个阶段维护 32 个固定槽位，每个槽位保存完整观察键、是否失败及重复次数，不使用哈希或动态容量。

- 新成功事件：写一次 IORegistry 快照并打印一条 `DCCDIAG` 日志。
- 新失败事件：写一次 IORegistry 快照并打印一条 `DCCDIAG` 日志。
- 重复成功事件：累计 duplicate；仅当该槽位的重复次数为 `1、2、4、8...` 时刷新 IORegistry，不打印日志。
- 重复失败事件：累计 duplicate 与 error；仅当重复次数为 `1、2、4、8...` 时刷新 IORegistry 并打印一条带 repeat 次数的摘要日志。
- 阶段容量溢出：累计 overflow；仅在 overflow 次数为 `1、2、4、8...` 时发布并打印摘要。

因此，完全重复的逐帧调用只执行锁内整数更新和固定数组比较；IORegistry 分配与日志数量随重复次数按对数增长。

## 原子快照

诊断服务继续使用一个 `IOLock`。观察决策、sequence 分配、快照字典构建、IORegistry 替换和日志输出在同一锁保护下按顺序完成，避免 sequence 回退及字段混合。

运行时只保留以下顶层状态属性：

- `NootRX_DCCDiagEnabled`
- `NootRX_DCCDiagRouteMask`
- `NootRX_DCCDiagFailureCode`
- `NootRX_DCCDiagSnapshot`

`NootRX_DCCDiagSnapshot` 是一次性替换的 `OSDictionary`，包含 sequence、四类累计计数、阶段、重复次数以及本次完整标量快照。禁止发布内核指针、GPU 地址和 `pMipInfo` 地址；只记录 `pMipInfo` 是否存在。

## 安全路由

运行时入口继续精确门控到：

```text
macOS build 25E253
device-id 0x73DF
pci-revision 0xC0
-NRXPowerDiag
```

每个驱动组件按以下顺序处理：

1. 解析该组件需要的全部符号，不安装 route。
2. 对四个函数的入口以及 `AMDHWAlignManager2::init` 的三个虚表 getter 调用、Framebuffer request `0x1A` 分支进行逐字节关键指令校验。
3. 校验全部通过后再安装该组件的 route。
4. 任一解析、指令校验或 route 安装失败时，调用诊断服务的禁用入口，发布失败代码并记录一次日志；已安装的透明 wrapper 只继续调用原函数，不再记录。

加速器的三个符号必须在同一次 `routeMultiple` 中安装，使符号解析在任何代码改写前完成。Framebuffer 路由独立安装。诊断失败不得改变正常 NootRX 补丁和 Apple DCC 路径。

## 快照字段

除现有字段外，增加以下标量：

- AddrLib 输入：`dccKeyFlags`、`colorFlags`、`resourceType`、`numSlices`、`numFrags`、`numMipLevels`、`dataSurfaceSize`、`firstMipIdInTail`。
- AddrLib 输出：`depth`、compress block 三维尺寸、`metaBlkNumPerSlice`、`dccRamSliceSize`、`alignmentPadding`、`pMipInfoPresent`。
- Framebuffer 浮点宽高：记录原始 IEEE-754 位模式，不执行未校验的浮点转整数转换。
- 路由状态：组件 route mask 与失败代码。

日志与 IORegistry 字典使用相同的 immutable snapshot，确保两份证据表达同一事件。

## 测试与验收

- 策略单元测试验证成功重复、失败重复、overflow 的 `1、2、4、8...` 检查点。
- 单元测试验证四个阶段容量彼此独立。
- 指令校验测试对正确 fixture 通过，对任一关键字节变化拒绝。
- 集成守卫验证精确 build gate、安全禁用接口、加速器批量 route、Framebuffer route 和原函数单次调用顺序。
- 所有既有 PowerProfile、DCC、DDI capability 和 GC policy 测试继续通过。
- clean Release x86_64 构建成功，产物包含诊断状态键与四个 route 符号。

## 实机边界

本阶段只生成低开销诊断 kext，不自动替换 EFI。构建验证完成后仍先使用：

```text
-NRXPowerDiag
nootrx-gpu-dcc-displayable=0
```

确认诊断版没有新增卡顿后，再由用户明确批准唯一一次 Displayable DCC 开启复现。
