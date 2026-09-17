# Tahoe Navi22 原生 capability 保留设计

## 问题与证据

macOS Tahoe 26.4.1 上，RX 6750 XT 会在不同应用和 Metal/OpenGL 提交路径中触发 GFX channel reset。最新一次报告为 `Channel 34 GFX`，同时阻塞 IDEA 与 WindowServer；报告显示 shader 内容校验通过、无 VM protection fault、无 RSMU timeout，而 CP Graphics 与 Graphics Engine Setup 保持 busy。

Tahoe 自带 Navi23 donor 的 16 项 DDI capability 表中，第 11 项为 `0x42000020`。当前 NootRX 在 HWLibs 与 X6000Framebuffer 两条路径找到 donor 后，都会将其 `caps` 指针强制替换为通用表；通用表对应项为 `0x42040028`。历史 Navi22 专用表的对应项同样是 `0x42000020`。

因此本次仅验证一个假设：Tahoe 的原生 capability 被旧通用表覆盖，导致 Navi22 在 Tahoe 图形提交路径中启用了不匹配的能力位。

## 已选方案

仅当系统为 Tahoe 且 GPU 为 Navi22 时，保留已定位 donor entry 的原生 `caps` 指针：

1. X6000Framebuffer 路径继续复用已找到的 Tahoe Navi23 donor，但不再替换其 `caps`。
2. HWLibs 路径继续修改设备标识、revision 和 golden settings，但不再替换 donor 的 `caps`。
3. `CAILAsicCapsInitTable` 继续引用最终选定的同一 `caps` 指针，防止两张表产生不一致。
4. 非 Tahoe 或非 Navi22 路径保持现有通用 capability 行为。
5. 启动日志记录路径、donor 设备号与最终 capability 第 11 项；诊断只观察选择结果，不改变图形功能。

该方案不禁用 Metal、OpenDesign 或 VideoToolbox，也不修改 GC selector、PowerPlay workaround、固件选择或 OpenCore 电源参数。

## 未选方案

- 全平台恢复静态 Navi22 capability：影响 Sonoma、Sequoia 等当前未出现该 Tahoe 故障的路径，范围过大。
- 仅修改 `0x42040028` 的单个 bit：相关 bit 的 Apple 内部语义尚未确认，直接掩码会把推测写入实现。
- 同时调整 GC、PowerPlay 或 SDMA 固件：会引入多个验证变量，无法判断 capability 修补是否有效。

## 代码边界

新增一个无状态 capability 选择函数，输入为当前 donor 指针、是否 Tahoe、是否 Navi22，输出为最终 `caps` 指针。两个 kext patch 路径共同调用该函数，避免各自复制条件判断。函数及关键写入步骤添加用途注释和诊断日志。

## 测试与验证

1. 单元测试先证明当前实现缺少 Tahoe/Navi22 保留规则。
2. 验证 Tahoe + Navi22 返回 donor 原生指针及 `0x42000020`。
3. 验证 Tahoe + 非 Navi22、非 Tahoe + Navi22 仍返回通用表。
4. 构建 Release kext，检查产物和 Info.plist 可读取、二进制包含诊断字符串。
5. 部署后从启动日志确认两个路径最终使用 `0x42000020`。
6. 实机运行验证仍以是否再次出现 `.gpuRestart`、GFX hang 或 WindowServer watchdog 为准；构建和启动成功不等于已证明长期死机根因修复。

## 回滚

补丁为独立提交。若出现启动失败、图形加速丢失或新类型 GPU reset，可恢复上一版 NootRX.kext；OpenCore 其他配置不需要同步回滚。
