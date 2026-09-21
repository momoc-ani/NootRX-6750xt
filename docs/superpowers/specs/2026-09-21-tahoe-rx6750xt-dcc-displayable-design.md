# Tahoe RX 6750 XT GPUDCCDisplayable 独立开关设计

## 背景

macOS Tahoe、RX 6750 XT（PCI device `0x73DF`、revision `0xC0`）在 Chromium/Skia GPU Rasterization 路径下会偶发局部彩色矩形闪烁。现有对照结果如下：

```text
默认 Chrome                               -> 闪烁
禁用 zero-copy                            -> 闪烁
禁用全部 GPU 加速                         -> 不闪烁
仅禁用 GPU Rasterization                  -> 不闪烁
Safari                                    -> 不闪烁
```

当前证据只把故障边界定位到 Chromium/Skia GPU Rasterization 与 AMD Metal 驱动的交互，尚未证明底层缺陷一定来自 DCC。`GPUDCCDisplayable` 开关用于进行单变量驱动侧验证，不作为默认修复。

## 目标

- 增加独立 boot-arg：`nootrx-gpu-dcc-displayable=0|1`。
- 仅在 macOS Tahoe 与 RX 6750 XT `0x73DF/0xC0` 组合下允许覆盖。
- 未传参数时不修改现有 `GPUDCCDisplayable=true` 行为。
- 将最终值及是否发生覆盖发布到 IORegistry，并写入启动日志。
- 不修改 PowerPlay、DDI capability、GC 兼容选择、SDMA 固件、Metal、OpenDesign 或 VideoToolbox 路径。

## 非目标

- 不把该实验开关设为默认关闭 DCC。
- 不声称该开关能够修复 GPU Reset、GFX hang 或 WindowServer watchdog。
- 不修改 Chromium、Skia 或页面代码。
- 不增加 `ResNoAllocPaging` 等其他实验变量。

## 方案选择

采用修改注入的 `AMDRadeonX6000` accelerator personality 的方案。`GPUDCCDisplayable` 是该 personality 的顶层属性，Metal accelerator 启动后也在对应 IORegistry 服务上发布该值，因此在 personality 注入前修改是最直接且最小的路径。

不采用以下方案：

- 仅向 PCI provider 写同名属性：不能保证 accelerator personality 会采用 provider 上的值。
- 运行时 patch Apple Metal 驱动：侵入性和 Tahoe 版本耦合明显更高，当前证据不足。

## 配置模型

增加一个独立、可单元测试的配置对象，默认状态为：

```text
value = 1
overridden = false
```

参数处理规则：

| 环境 | 参数 | 最终值 | overridden | personality 是否改写 |
|---|---:|---:|---:|---|
| Tahoe + `0x73DF/0xC0` | 未传 | 1 | false | 否 |
| Tahoe + `0x73DF/0xC0` | 0 | 0 | true | 是，写入 false |
| Tahoe + `0x73DF/0xC0` | 1 | 1 | true | 是，显式写入 true |
| Tahoe + `0x73DF/0xC0` | 其他值 | 1 | false | 否，记录忽略日志 |
| 其他系统或设备 | 任意值 | 1 | false | 否 |

## Personality 匹配与注入

只处理 `com.apple.kext.AMDRadeonX6000` 注入数组中的 Navi23 accelerator personality，并要求其 `IOPCIMatch` 包含 `0x73DF1002`。不修改同一数组中的 Navi21 personality，也不修改 framebuffer 或 HWServices personality。

仅当配置对象的 `overridden=true` 时才替换 personality 顶层的 `GPUDCCDisplayable`。这样没有 boot-arg 时，注入数据与当前稳定版本完全一致。

## IORegistry 与日志

在 RX 6750 XT 的 PCI 服务上发布：

```text
NootRX_GPUDCCDisplayable = 0 | 1
NootRX_GPUDCCDisplayableOverride = 0 | 1
```

启动日志包含：

```text
device=0x73DF pciRev=0xC0 os=Tahoe value=<0|1> override=<yes|no>
```

当参数值不是 `0` 或 `1` 时，额外记录参数被忽略，最终值保持 `1`。

## 测试策略

### 单元测试

- 默认值为 `1`，且未覆盖。
- `0` 和 `1` 都能形成独立有效覆盖。
- 非法值不能改变默认配置。

### 集成守卫

- 验证 accelerator personality 修改只位于 `AMDRadeonX6000` 注入分支。
- 验证代码检查 Tahoe、`0x73DF` 与 `0xC0`。
- 验证仅在 `overridden=true` 时改写 `GPUDCCDisplayable`。
- 验证两个 IORegistry 属性和启动日志存在。

### 构建验证

- 运行现有 PowerProfile、DDI capability 与 GC policy 测试，确认无回归。
- 运行新增测试。
- 使用 Release x86_64 配置构建 `NootRX.kext`。

## 实机验证边界

构建成功只能证明代码和静态策略正确，不能证明 DCC 是闪烁根因。实机验证需要：

1. 使用 `nootrx-gpu-dcc-displayable=0` 启动。
2. 确认 IORegistry 两个属性分别为 `0` 和 `1`。
3. 临时把 Chrome GPU Rasterization 恢复为默认值。
4. 使用同一 Holopix 页面复现路径观察。
5. 同时检查是否出现 `GFX is hung`、GPU Reset 或 WindowServer watchdog；出现任何一项立即回滚。

回滚方式是删除 boot-arg。删除后 personality 不再被改写，恢复当前稳定行为。
