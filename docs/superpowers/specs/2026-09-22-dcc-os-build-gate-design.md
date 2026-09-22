# Tahoe DCC 诊断 build gate 数据源修正设计

## 背景与证据

Tahoe `25E253`、RX 6750 XT `0x73DF/0xC0` 已带
`-NRXDCCDiag` 启动，但运行态显示：

```text
NootRX_DCCDiagRequested = 1
NootRX_DCCDiagOSBuildMatch = 0
NootRX_DCCDiagEnabled = 0
NootRX_DCCDiagRouteMask = 0
NootRX_DCCDiagFailureCode = 0
```

系统运行后的 IORegistry 根节点同时存在：

```text
OS Build Version = 25E253
```

因此已确认拒绝发生在 route 安装前的 build gate。现有实现直接读取内核全局
`osversion`；本修正不把其初始化时序写成既定根因，只替换为当前系统已经公开的
IORegistry 精确 build 数据源。

## 方案比较

1. **使用 IORegistry 根节点的 `OS Build Version`，缺失时 fail closed。**
   修改范围最小，继续进行精确字符串比较，也不会在无法确认 build 时安装专用
   route。本次采用该方案。
2. IORegistry 缺失时回退到全局 `osversion`。该方案保留了当前无法解释的输入，
   会让门控来源重新产生歧义，因此不采用。
3. 延后整个 DCC recorder 初始化，等待系统进入更晚生命周期。该方案会改变 route
   安装时序并扩大生命周期耦合，当前没有证据证明有必要，因此不采用。

## 已批准设计

- 在 `NootRX.cpp` 增加一个单一职责的内部读取函数：
  - 通过 `IORegistryEntry::getRegistryRoot()` 获取根节点；
  - 使用 `kOSBuildVersionKey` 获取属性；
  - 仅接受 `OSString`；
  - 根节点、属性或字符串缺失时返回 `nullptr`。
- `processPatcher()` 只读取一次 build，并将同一个值同时传给
  `DCCDiagnosticsPolicy::observeGate()` 和 `isTarget()`，避免观察值与实际门控使用
  不同数据源。
- 删除 DCC gate 对全局 `osversion` 的依赖，不提供回退。
- `DCCDiagnosticsPolicy` 的精确 `25E253` 比较、Tahoe、device ID、PCI revision 和
  `-NRXDCCDiag` 条件保持不变。
- route 的符号解析、指令签名校验、失败代码和运行时 recorder 行为保持不变。
- 请求了 DCC 诊断但根属性缺失或 build 不匹配时记录一条简短日志；仍保持
  `Enabled=0`。

## 测试与验证

TDD 红灯首先修改集成守卫，要求：

- 源码读取 `IORegistryEntry::getRegistryRoot()` 和 `kOSBuildVersionKey`；
- `observeGate()` 与 `isTarget()` 使用同一个局部 build 值；
- DCC 门控路径不再引用全局 `osversion`；
- 不存在回退路径。

随后只添加使该守卫通过的最小实现。完成后运行：

- `DCCDiagnosticsPolicyTests`；
- 全部现有 C++ 主机测试；
- 全部 shell 集成守卫；
- clean Release x86_64 构建；
- `Info.plist` 校验与最终二进制标记检查。

实际启动验证不属于代码完成条件。部署后必须看到：

```text
NootRX_DCCDiagRequested = 1
NootRX_DCCDiagOSBuildMatch = 1
NootRX_DCCDiagEnabled = 1
NootRX_DCCDiagRouteMask = 3
NootRX_DCCDiagFailureCode = 0
```

任何一项不满足都停止复现，不开启 Displayable DCC。

## 明确不在本次范围

- 不修改 Displayable DCC 状态；
- 不修改 ULV、GFXOFF、Falcon Quick Transition 或 WorkLoadPolicyMask；
- 不修改 donor 映射、GC/DDI capability 或 GFX command submission；
- 不把本次门控修正描述为 GPU Reset 修复。
