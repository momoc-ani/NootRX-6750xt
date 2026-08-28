# RX 6750 XT 电源路径定位实现计划

> **面向 AI 代理的工作者：** 在当前会话中按测试驱动方式逐项实现并验证。

**目标：** 保持现有 RX 6750 XT 稳定配置为默认值，同时允许通过四个独立 boot-arg 恢复单项原始行为，并在 GPU panic/reset 相关路径留下可关联的配置和驱动日志。

**架构：** 将 PowerPlay 配置值提取为不依赖 IOKit 的小型数据结构，由主机侧单元测试验证默认值和独立覆盖行为。NootRX 在注入 RX 6750 XT framebuffer personality 前读取 boot-arg、写入四个属性并记录最终配置；可选诊断开关负责启用 PowerPlay/DAL 日志及 GPU panic 上下文，不改变默认稳定路径。

**技术栈：** C++17、Lilu boot-arg API、IOKit、Xcode kext 构建、主机侧 clang++ 测试。

---

### 任务 1：可测试的 PowerPlay 配置

**文件：**
- 创建：`NootRX/PowerProfile.hpp`
- 创建：`Tests/PowerProfileTests.cpp`

- [ ] 编写失败测试，覆盖稳定默认值、原始值以及四项独立覆盖。
- [ ] 运行测试并确认因配置模块缺失而失败。
- [ ] 实现最小的配置数据结构和覆盖方法。
- [ ] 重新运行测试并确认通过。

### 任务 2：boot-arg 与属性注入

**文件：**
- 修改：`NootRX/NootRX.hpp`
- 修改：`NootRX/NootRX.cpp`

- [ ] 读取四个数值 boot-arg，未提供时保持稳定默认值。
- [ ] 将最终值注入 `aty_properties`，同时镜像到 GPU IORegistry。
- [ ] 输出单行、固定字段顺序的启动日志，包含 profile mask 和四项最终值。

### 任务 3：崩溃路径诊断

**文件：**
- 修改：`NootRX/X6000FB.hpp`
- 修改：`NootRX/X6000FB.cpp`

- [ ] 增加独立 `-NRXPowerDiag` 开关，不默认开启高噪声日志。
- [ ] 诊断模式下启用已有 PowerPlay、DAL 和 BIOS parser 日志路径。
- [ ] GPU panic 时记录设备、revision、profile mask、四项值及原始错误信息，并调用原始 panic 路径。

### 任务 4：文档与验证

**文件：**
- 修改：`README.md`

- [ ] 记录四个 boot-arg 的语义、默认值和逐项实验顺序。
- [ ] 记录诊断开关、日志查询和报告收集命令。
- [ ] 运行单元测试、Release x86_64 构建并检查产物。
- [ ] 检查最终 diff，确保未修改用户现有未跟踪文件。

### 任务 5：交互式参数组合验证顺序

- [ ] 按 `docs/validation/2026-08-28-rx6750xt-power-matrix.md` 中的 Gray-code 顺序逐轮验证。
- [ ] 每轮开始前与用户确认，重启后核对实际 profile，再执行固定负载。
- [ ] 失败轮先收集 `.gpuRestart`、panic、DAL/PowerPlay 日志和 IORegistry，再恢复 Mask 0。
