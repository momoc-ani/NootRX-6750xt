# Tahoe Navi22 GC 运行时选择器拆分设计

## 背景与证据

2026-09-22 14:50，Tahoe 25E253 上的 RX 6750 XT 出现不可恢复的画面冻结。首份 GPU reset 报告显示：

- `mediaanalysisd` 的 Metal `ComputeUQ0` 命令未完成；
- 随后 IDEA、WindowServer 和 Chrome 的 GFX 提交被阻塞；
- Displayable DCC 当时为关闭状态；
- 首份报告没有 RSMU 超时，后续每次失败的 reset 固定增加一组 GC/UMC 事件，因此这些事件是复位失败的结果，不是最初触发点。

当前 NootRX 对 Navi22 的 `_gc_sw_init` 强制返回 GC 10.3.4，但固件文件仍来自 Navi22 GC 10.3.2。Tahoe 原始 `AMDRadeonX6810HWLibs` 的本机反汇编确认：

- GC 10.3.2 是原始二进制支持的运行时版本；
- `_gc_sw_init` 的版本值控制 GC 配置表、硬件配置表和函数指针选择；
- GC 10.3.4 会额外切换三条专用 RLC/固件 backdoor 函数；
- 10.3.2 与 10.3.4 使用不同的 RLC TOC 基址和不同的寄存器初始化集合。

因此当前最小且证据最充分的修复边界，是让真实 Navi22 恢复 GC 10.3.2 运行时路径，同时仅保留 Tahoe 固件描述符所需的 10.3.4 兼容映射。

## 已批准方案

仅对 Tahoe + Navi22/RX 6750 XT 拆分两个职责：

1. `_gc_sw_init` 不再应用 `kGcSwInit*` 的 10.3.4 常量替换，使用硬件报告的 GC 10.3.2。
2. `_gc_set_fw_entry_info` 相关补丁保持不变，继续使用 10.3.4 描述符兼容选择器。
3. 固件选择保持 Navi22 原生路径：GC 10.3.2、SDMA 5.2.2 和 Navi22 SMC。
4. 向 GPU IORegistry 节点发布：
   - `NootRX_GCRuntimeVersion = 0x0A0302`
   - `NootRX_GCDescriptorVersion = 0x0A0304`
5. 记录一条启动日志，说明 Tahoe Navi22 已拆分运行时与描述符选择器。

非 Tahoe 系统继续采用上游行为，不改变现有兼容范围。

## 不在本次范围内

本次不得同时修改以下内容：

- Displayable DCC 配置或 DCC 诊断 gate；
- `PP_DisableULV`、`PP_GfxOffControl`、`PP_Falcon_QuickTransition_Enable`、`PP_WorkLoadPolicyMask`；
- Tahoe Navi22 DDI capability 表；
- Chrome、IDEA 或 `mediaanalysisd` 的启动参数；
- GC 10.3.4 固件、SDMA 5.2.4 固件或 Navi23 SMC；
- OpenCore `config.plist` 和现有 boot-args。

## 备选方案及取舍

### 方案 A：拆分运行时与描述符选择器（已批准）

优点是修改范围最小，直接使用 Tahoe 原始二进制已存在的 GC 10.3.2 路径，同时保留固件描述符兼容。Metal、OpenDesign 和 VideoToolbox 不被关闭。

### 方案 B：保留 GC 10.3.4，再逐条改写 RLC/backdoor 函数

该方案需要复刻 Apple 私有结构布局和函数指针表，对 Tahoe 小版本高度敏感，修改面明显更大。本次不采用。

### 方案 C：加载 GC 10.3.4 固件

RX 6750 XT 是 Navi22，直接加载 Navi23 对应固件会扩大硬件不匹配风险。本次不采用。

## 数据流

```text
RX 6750 XT / Navi22 hardware version 10.3.2
        |
        +--> _gc_sw_init: 10.3.2 runtime tables and RLC/backdoor functions
        |
        +--> _gc_set_fw_entry_info: 10.3.4 descriptor compatibility
                                      |
                                      +--> load Navi22 GC 10.3.2 firmware files
```

## 测试与验证

实现必须遵循以下验证顺序：

1. 先修改 GC 策略测试，确认当前统一 10.3.4 行为导致测试失败。
2. 实现最小 Tahoe 条件分支，并确认该测试通过。
3. 验证非 Tahoe 路径仍应用 `_gc_sw_init` 兼容补丁。
4. 验证两套 `_gc_set_fw_entry_info` 描述符补丁保持存在。
5. 验证构建产物包含 GC 10.3.2 固件名称和新的拆分日志。
6. 运行现有 C++ 测试、Shell 集成守卫和 Release x86_64 构建。
7. 部署后读取两个 IORegistry 属性，确认运行时为 10.3.2、描述符为 10.3.4。
8. 实机继续保持 DCC 关闭和稳定电源基线；若再次出现 GPU reset，采集新报告并比较首个 pending channel 和 reset 恢复状态。

代码测试和构建成功只能证明补丁正确落地，不能提前宣称长期死机已经根治。长期结论以实机运行期间不再生成新的 GPU reset 或 WindowServer watchdog 报告为准。
