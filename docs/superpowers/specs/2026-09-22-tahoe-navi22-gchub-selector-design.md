# Tahoe Navi22 GCHub 选择器设计

## 目标

在 macOS Tahoe + RX 6750 XT/Navi22 上，仅将 Apple `AMDNavi23Hardware::newGCHub()` 的 GCHub 实例选择切换到系统已有的 `AMDNavi21Hardware::newGCHub()` 实现，验证 `GCHub_10_3_0` 是否能避免当前 `GCHub_10_3_4` 引发的 GFX hang；不改变 MMHub、accelerator 类、PSP、SMC、SDMA、电源参数、DCC 或 Chrome 参数。

## 已确认的事实

- 当前 IORegistry 实例化的是 `AMDRadeonX6000_AMDNavi23GraphicsAccelerator`。
- 当前实际 Hub 对象为 `AMDRadeonX6000_AMDGCHub_10_3_4` 和 `AMDRadeonX6000_AMDMMHub_2_1_1`。
- Apple 二进制中没有 `AMDGCHub_10_3_2` 类。
- Apple 的 Navi21 factory 返回 `AMDGCHub_10_3_0` 和 `AMDMMHub_2_1_0`。
- Navi21 与 Navi23 的 `newGCHub()` factory 都只分配并构造 Hub 对象，不读取 hardware 对象状态；两者硬件对象分配大小都为 `0x20850`。
- 2026-09-22 16:30 的第一份 reset 报告由 Chrome Dawn/WebGPU 的 GFX channel 触发，`dcc_en=0` 且没有 VM protection fault。
- 17:19 的第二份报告发生在前一次 reset 后，才出现 WindowServer 地址 `0x0` 的 VM fault 和 RSMU 事件。

## 方案边界

### 保留不变

- `AMDRadeonX6000_AMDNavi23GraphicsAccelerator` accelerator personality。
- Navi22 的 PSP、SMC、GC 10.3.2、SDMA 5.2.2 固件选择。
- `AMDNavi23Hardware::newMMHub()`，继续使用 `AMDMMHub_2_1_1`。
- 现有 GC runtime/descriptor 拆分：Tahoe runtime 10.3.2、descriptor 10.3.4。
- DCC 当前稳定基线、四项电源 workaround 和 OpenCore boot-args。
- Metal、OpenDesign、VideoToolbox，以及 Chrome 的应用配置。

### 新增行为

只在 `NootRXMain::callback->attributes.isNavi22()` 且 `getKernelVersion() == KernelVersion::Tahoe` 时：

1. 解析 Apple 二进制中的 `AMDRadeonX6000_AMDNavi21Hardware::newGCHub()` 地址。
2. 路由 `AMDRadeonX6000_AMDNavi23Hardware::newGCHub()` 到 NootRX wrapper。
3. wrapper 调用 Navi21 factory，返回 `AMDGCHub_10_3_0` 实例。
4. 发布以下诊断属性和日志：
   - `NootRX_GCHubDonor = "Navi21"`
   - `NootRX_GCHubClass = "AMDRadeonX6000_AMDGCHub_10_3_0"`
   - `Tahoe Navi22 GCHub donor: Navi21 / GCHub_10_3_0; MMHub unchanged`

非 Tahoe、非 Navi22 和失败路径保持原始行为；如果任一符号无法解析或路由失败，使用 `PANIC_COND` 中止加载，避免静默地使用未知 Hub 组合。

## 数据流

```text
Tahoe + Navi22
    |
    +--> X6000::processKext(AMDRadeonX6000)
            |
            +--> solve Navi21Hardware::newGCHub
            +--> route Navi23Hardware::newGCHub
            +--> wrapper(that)
                    |
                    +--> Navi21Hardware::newGCHub(that)
                            |
                            +--> GCHub_10_3_0
                    |
                    +--> Navi23Hardware::newMMHub unchanged
                            |
                            +--> MMHub_2_1_1
```

## 代码修改范围

- `NootRX/X6000.hpp`
  - 增加 `NewGCHubFunction` 类型、donor factory 指针、wrapper 声明。
  - 增加 GCHub 选择器测试所需的常量和注释。
- `NootRX/X6000.cpp`
  - 在 Tahoe + Navi22 条件下解析 Navi21 factory 并路由 Navi23 factory。
  - wrapper 调用 donor factory，并写入实际选择日志/属性。
- `NootRX/NootRX.hpp`、`NootRX/NootRX.cpp`
  - 增加发布 GCHub 选择诊断属性的方法；方法只负责 IORegistry 与日志，不参与 Hub 选择。
- `Tests/GCHubPolicyTests.sh`
  - 静态守卫 Tahoe 条件、Navi21 donor 符号、Navi23 route、MMHub 未改变以及诊断字符串。
- `README.md`
  - 记录该补丁是单独的 GCHub 验证，不等于已证明长期稳定；明确回退边界和实机观察项目。

## 测试与验证

### 源码/构建验证

1. 旧代码上测试必须失败，证明守卫检测到缺失的 GCHub donor 路径。
2. 新代码上测试通过，且确认 route 只出现在 Tahoe + Navi22 分支。
3. 运行全部 C++ 测试和 shell 守卫。
4. 构建 Release x86_64 kext，检查二进制包含 GCHub donor 日志与类名。

### 实机验证顺序

1. 用户挂载 EFI 后，仅替换 `NootRX.kext`，不修改 config.plist/boot-args。
2. 重启后读取 IORegistry，确认：
   - `NootRX_GCHubDonor = Navi21`
   - `NootRX_GCHubClass = AMDRadeonX6000_AMDGCHub_10_3_0`
   - 仍存在 `AMDRadeonX6000_AMDMMHub_2_1_1`
3. 检查启动日志中出现 GCHub donor 选择记录。
4. 保持当前 Chrome 参数为空，进行普通 Chrome、IDEA、OpenDesign、Metal、视频播放和显示器休眠/唤醒操作。
5. 若发生 GPU reset，优先比较首个 pending channel、是否仍为 Dawn、`dcc_en`、VM fault 与 RSMU 初始状态。

### 成功标准

- 只证明 GCHub 选择器已按预期生效，且没有改变 MMHub/PSP/电源/DCC 路径。
- 若实机观察期内不再出现第一现场 GFX hang，才可把该假设视为得到支持。
- 不以单次启动、构建通过或 Chrome 不崩溃宣称长期根治。

## 回退

若新 GCHub 导致启动失败、Metal/OpenDesign 不可用、性能回退或出现更早的 reset，恢复 EFI 中的 `NootRX.kext.backup-20260922-gc-runtime-split`，代码保留在分支中供比较；不删除用户现有配置。

