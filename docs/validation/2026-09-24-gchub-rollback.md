# 2026-09-24 Tahoe Navi22 GCHub 回退验证

## 目的

验证 2026-09-22 加入的 Navi21 `AMDGCHub_10_3_0` donor 路由是否导致
RX 6750 XT 在 Tahoe 上更早进入 GPU reset/recovery 失败路径，并恢复到
Navi23 原生 `AMDGCHub_10_3_4`。

## 实机证据

本次 donor 版本在 10:47:30–10:49:05 连续产生四次 GPU reset：

1. Chrome Helper / Metal / GFX channel 31 首先挂起；
2. 首次报告出现 Chrome 地址 `0x00000004b87c9000` 的 VM `READ/NACK`；
3. 第二次报告同一地址变为 `FREE`，并记录 2 次 GC/UMC RSMU timeout；
4. 后续两次 reset 没有关联应用，随后出现画面棋盘格和冻结。

该运行同时确认 `GPUDCCDisplayable=No`、`dcc_en=0`，三项 PowerPlay 稳定参数
仍为关闭 ULV/GFXOFF/Falcon Quick Transition；因此本次回退只针对 GCHub donor。

## 代码变更

- 删除 Tahoe + Navi22 的 Navi21 `newGCHub()` donor 解析和 Navi23 factory route；
- 删除 `wrapNewGCHub`、donor 地址字段及 `NootRX_GCHub*` 诊断属性；
- 恢复 Navi23 原生 `AMDGCHub_10_3_4` 选择；
- 保留 GC runtime/descriptor split、DDI caps、DCC、PowerPlay、固件、Metal、
  OpenDesign、VideoToolbox 和 Chrome 无启动参数基线。

## 验证结果

- `Tests/GCHubPolicyTests.sh`：通过，确认不再包含 Navi21 donor route；
- 其余 shell guards：全部通过；
- C++ policy/validation tests：全部通过；
- Release x86_64 kext 构建：通过，产物为临时构建目录中的
  `build/Products/Release/NootRX.kext`；
- 构建产物字符串检查：未发现 `GCHub_10_3_0`、`NootRX_GCHubDonor` 或
  `NootRX_GCHubClass`，仍包含 GC split、DCC 和 PowerPlay 基线字符串。

## 部署后观察

EFI 替换后保持 Chrome 启动参数为空。记录下一次 `.gpuRestart` 的时间、首个
`Application`、`Restart Channel`、VM fault、RSMU timeout 和 `dcc_en`，与本次
donor 版本报告对比；在没有新的实机运行证据前，不宣称已经根治 GPU reset。
