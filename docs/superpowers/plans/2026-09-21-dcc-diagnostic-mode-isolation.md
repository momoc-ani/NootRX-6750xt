# DCC 诊断模式隔离实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框跟踪进度。

**目标：** 将低开销 DCC 诊断与全量 PowerPlay/DAL 调试解耦，并按带进度的测试计划验证低开销 recorder 与安全路由两个功能。

**架构：** 无内核依赖的模式策略负责两个独立开关；`NootRXMain` 只负责读取 boot-arg 和分发状态。既有 DCC recorder、安全路由及 PowerPlay 调试实现保持不变。

**技术栈：** C++17、Lilu、OpenCore plist、clang++ 主机测试、shell 集成守卫、xcodebuild。

---

## 文件职责

- 创建 `NootRX/DiagnosticsModePolicy.hpp`：表达 Power 与 DCC 两个独立诊断请求。
- 创建 `Tests/DiagnosticsModePolicyTests.cpp`：验证四种参数组合没有隐式耦合。
- 修改 `NootRX/NootRX.hpp/.cpp`：读取 `-NRXDCCDiag` 并把 DCC target gate 切换到独立状态。
- 修改 `Tests/DCCDiagnosticsIntegrationTests.sh`：验证实际源码和构建二进制包含独立参数及正确 wiring。
- 修改 `README.md` 与 DCC runbook：记录两个参数的职责和稳定基线。
- 创建 `docs/validation/2026-09-21-dcc-diagnostics-two-feature-progress.md`：记录测试计划、进度和证据。
- 修改 EFI `config.plist`：仅把 `-NRXPowerDiag` 替换为 `-NRXDCCDiag`。
- 替换 EFI `NootRX.kext`：部署重新构建且验证通过的产物。

### 任务 1：独立诊断模式策略

- [ ] 编写四种组合的失败测试：none、DCC-only、Power-only、both。
- [ ] 运行测试并确认缺少策略头文件。
- [ ] 实现最小 `DiagnosticsModePolicy::select`。
- [ ] 运行测试确认四种组合通过。
- [ ] 提交独立模式策略。

### 任务 2：NootRX boot-arg wiring

- [ ] 扩展集成守卫，要求 `-NRXDCCDiag`、独立 DCC 状态和二进制标记。
- [ ] 运行守卫并确认旧实现失败。
- [ ] 修改 `NootRXMain`，让 `-NRXPowerDiag` 与 `-NRXDCCDiag` 分别驱动各自状态。
- [ ] 运行模式测试、DCC 策略测试与集成守卫。
- [ ] 提交 boot-arg wiring。

### 任务 3：测试计划与进度文档

- [ ] 写明两个功能、自动化/实机边界、通过条件和停止条件。
- [ ] 写入 17:39 GPU Reset 证据，明确 DCC 关闭仍会 reset。
- [ ] 建立逐阶段状态表，并在每个阶段后更新证据与时间。
- [ ] 更新 README 和原 DCC runbook 的 boot-arg。
- [ ] 提交文档。

### 任务 4：完整验证、推送与 EFI 部署

- [ ] 运行全部主机测试和 shell 集成守卫。
- [ ] clean Release x86_64 构建，校验 plist 与二进制。
- [ ] 推送 `dcc-root-cause-diagnostics`。
- [ ] 备份 EFI 当前 kext/config，替换 kext，并只修改诊断 boot-arg。
- [ ] 对 EFI kext、config 和参数做落盘验证。
- [ ] 更新进度文档为“等待重启”，重新提交并推送。

### 任务 5：重启后实机验证

- [ ] 确认当前加载的 kext UUID/构建产物与 EFI 一致。
- [ ] 确认 `-NRXDCCDiag` 存在且 `-NRXPowerDiag` 不存在。
- [ ] 确认 `Enabled=1`、`RouteMask=3`、`FailureCode=0`。
- [ ] 确认没有 NootRX 全量 PowerPlay/DAL enablement 日志。
- [ ] 按正常使用观察卡顿或 reset，并在用户每次反馈后更新进度文档。
- [ ] 基线通过后，再由用户决定是否执行 DCC `=1` 根因实验。

