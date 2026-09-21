# Tahoe DCC 诊断低开销与安全路由实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 消除 DCC 诊断在资源分配热路径中的逐调用日志和 IORegistry 开销，保证多线程证据顺序，并在 Apple ABI 或 route 不匹配时安全关闭诊断。

**架构：** 纯 C++ 策略层负责四阶段固定容量缓存和对数级检查点；kernel-only recorder 在同一锁内生成单个 `OSDictionary` 快照；独立的逐字节验证策略在安装 route 前验证 25E253 函数入口、虚表 getter 与 request `0x1A` 分支。wrapper 继续只调用 Apple 原函数一次并原样返回。

**技术栈：** C++17、Lilu `KernelPatcher`、IOKit `OSDictionary`/`OSNumber`、clang++ 主机测试、shell 集成守卫、xcodebuild。

---

## 文件职责

- 修改 `NootRX/DCCDiagnosticsPolicy.hpp`：四阶段缓存、重复/失败/overflow 检查点与决策结构。
- 修改 `Tests/DCCDiagnosticsPolicyTests.cpp`：验证对数限频和阶段容量隔离。
- 创建 `NootRX/DCCRouteValidation.hpp`：保存 25E253 关键指令及无哈希逐字节验证。
- 创建 `Tests/DCCRouteValidationTests.cpp`：验证正确 fixture 与单字节漂移。
- 修改 `NootRX/DCCDiagnostics.hpp/.cpp`：安全禁用、route 状态、完整快照和单字典原子发布。
- 修改 `NootRX/X6000.hpp/.cpp`：显式原函数 ABI、加速器预校验和批量安全 route。
- 修改 `NootRX/X6000FB.hpp/.cpp`：显式原函数 ABI、Framebuffer 预校验和安全 route。
- 修改 `Tests/DCCDiagnosticsIntegrationTests.sh`：验证安全 route、禁用路径和四个 wrapper 顺序。
- 修改 `README.md` 与诊断运行手册：记录低开销规则、新 IORegistry 结构和失败状态。

### 任务 1：四阶段对数限频策略

**文件：**
- 修改：`NootRX/DCCDiagnosticsPolicy.hpp`
- 修改：`Tests/DCCDiagnosticsPolicyTests.cpp`

- [ ] **步骤 1：编写失败的检查点与容量隔离测试**

增加断言：新事件 `shouldLog=true/shouldPublish=true`；重复成功在 repeat `1、2、4` 时只 publish，在 `3` 时均为 false；重复失败在相同检查点 log+publish；填满 Scanout 的 32 个槽后，新的 Framebuffer 观察仍作为新事件记录。

```cpp
const auto first = cache.observe(key, false);
assert(first.shouldLog && first.shouldPublish && first.repeatCount == 0);
assert(cache.observe(key, false).shouldPublish);  // repeat 1
assert(cache.observe(key, false).shouldPublish);  // repeat 2
assert(!cache.observe(key, false).shouldPublish); // repeat 3
assert(cache.observe(key, false).shouldPublish);  // repeat 4
```

- [ ] **步骤 2：运行测试并确认旧策略失败**

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-policy-red.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDiagnosticsPolicyTests.cpp -o "$test_dir/dcc-policy-tests"
```

预期：编译失败，提示缺少 `shouldPublish`、`repeatCount` 或阶段独立缓存行为不成立。

- [ ] **步骤 3：实现最小四阶段缓存**

把每个槽定义为完整 key、failure 与 repeatCount；每个 `DCCDiagnosticStage` 使用 32 个独立槽。增加：

```cpp
struct DCCObservationDecision {
    bool shouldLog;
    bool shouldPublish;
    bool overflow;
    uint32_t sequence;
    uint32_t repeatCount;
};
```

检查点函数固定为 `value != 0 && (value & (value - 1)) == 0`。只有需要发布时递增 sequence。所有方法添加用途注释。

- [ ] **步骤 4：运行策略测试并确认通过**

运行步骤 2 的编译命令并执行产物，预期退出码 0。

- [ ] **步骤 5：提交策略变更**

```sh
git add NootRX/DCCDiagnosticsPolicy.hpp Tests/DCCDiagnosticsPolicyTests.cpp
git commit -m "fix: bound DCC diagnostic hot paths"
```

### 任务 2：25E253 关键指令校验

**文件：**
- 创建：`NootRX/DCCRouteValidation.hpp`
- 创建：`Tests/DCCRouteValidationTests.cpp`

- [ ] **步骤 1：编写失败的逐字节校验测试**

测试构造四段 25E253 fixture：align init 包含 `0x140/0x1B8/0x1A8` 三个 getter，Framebuffer fixture 包含入口、request `0x1A` 分支和 jump-table entry。每个 fixture 先通过，再翻转一个关键字节并确认拒绝。

```cpp
assert(DCCRouteValidation::validateAlignManagerInit(buffer, sizeof(buffer)));
buffer[0xC6] ^= 0x01;
assert(!DCCRouteValidation::validateAlignManagerInit(buffer, sizeof(buffer)));
```

- [ ] **步骤 2：运行测试并确认缺少验证头文件**

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-route-red.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCRouteValidationTests.cpp -o "$test_dir/dcc-route-tests"
```

预期：编译失败，提示缺少 `DCCRouteValidation.hpp` 或验证函数。

- [ ] **步骤 3：实现固定偏移的关键指令校验**

提供以下无分配接口：

```cpp
bool validateAlignManagerInit(const uint8_t *function, size_t available);
bool validateShouldAllocScanoutDcc(const uint8_t *function, size_t available);
bool validateGetDccInfo2(const uint8_t *function, size_t available);
bool validateFramebufferRequest(const uint8_t *function, size_t available);
```

内部只做边界检查和 `memcmp`，不使用哈希。每个函数和关键 pattern 都写用途注释。

- [ ] **步骤 4：运行 route 校验测试并确认通过**

运行步骤 2 的命令并执行产物，预期退出码 0。

- [ ] **步骤 5：提交验证策略**

```sh
git add NootRX/DCCRouteValidation.hpp Tests/DCCRouteValidationTests.cpp
git commit -m "test: validate Tahoe DCC route instructions"
```

### 任务 3：原子 IORegistry 快照与完整字段

**文件：**
- 修改：`NootRX/DCCDiagnostics.hpp`
- 修改：`NootRX/DCCDiagnostics.cpp`
- 修改：`Tests/DCCDiagnosticsIntegrationTests.sh`

- [ ] **步骤 1：扩展集成守卫并确认旧实现失败**

守卫必须要求 `NootRX_DCCDiagSnapshot`、`NootRX_DCCDiagRouteMask`、`NootRX_DCCDiagFailureCode`、`disable`、`markRouteReady` 存在；禁止旧的逐字段 `NootRX_DCCDiagLast*` 发布；要求 Framebuffer 宽高按原始位模式记录。

```sh
rg -F 'NootRX_DCCDiagSnapshot' "$diagnostics_effective" >/dev/null ||
    fail "atomic DCC snapshot property is missing"
if rg -F 'NootRX_DCCDiagLastWidth' "$diagnostics_effective" >/dev/null; then
    fail "legacy per-field DCC snapshot is still published"
fi
```

- [ ] **步骤 2：运行集成守卫并确认红灯**

```sh
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：FAIL，提示缺少原子快照属性。

- [ ] **步骤 3：实现 route 状态和单字典发布**

新增失败码枚举、`disable(code, reason)` 与 `markRouteReady(mask)`。`publishObservation` 在一个锁区间内完成 cache 决策、sequence、字典构建、一次 `setProperty("NootRX_DCCDiagSnapshot", dictionary)` 和可选日志；无需发布时立即解锁返回。

- [ ] **步骤 4：补全 snapshot 字段**

增加已批准的 AddrLib 输入/输出字段、`pMipInfoPresent` 以及 Framebuffer `widthBits/heightBits`。使用 `memcpy` 保存 float 位模式，不把未校验 float 转成整数。

- [ ] **步骤 5：运行策略与集成测试**

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-recorder-green.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDiagnosticsPolicyTests.cpp -o "$test_dir/dcc-policy-tests"
"$test_dir/dcc-policy-tests"
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：全部退出码 0。

- [ ] **步骤 6：提交 recorder 变更**

```sh
git add NootRX/DCCDiagnostics.hpp NootRX/DCCDiagnostics.cpp Tests/DCCDiagnosticsIntegrationTests.sh
git commit -m "fix: publish atomic DCC diagnostic snapshots"
```

### 任务 4：安全 route 与显式 ABI

**文件：**
- 修改：`NootRX/X6000.hpp`
- 修改：`NootRX/X6000.cpp`
- 修改：`NootRX/X6000FB.hpp`
- 修改：`NootRX/X6000FB.cpp`
- 修改：`Tests/DCCDiagnosticsIntegrationTests.sh`

- [ ] **步骤 1：增加安全 route 和四 wrapper 顺序守卫**

集成测试要求 accelerator 使用一个 `KernelPatcher::RouteRequest[]` 与一次 `patcher.routeMultiple`；要求符号验证发生在 route 前；要求失败调用 `disable` 而非 `PANIC_COND`；为 align、scanout、getDccInfo2、Framebuffer 四个 wrapper 检查 call-record-return 顺序。

- [ ] **步骤 2：运行守卫并确认旧 route 失败**

```sh
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：FAIL，提示仍使用会 panic 的诊断 route。

- [ ] **步骤 3：实现显式原函数 ABI**

在两个类中定义原函数指针类型并直接调用 typed pointer，移除四个诊断 wrapper 对 `FunctionCast` 的依赖。类型由编译器约束参数和返回值。

- [ ] **步骤 4：实现解析、关键指令校验和安全安装**

先用 `SolveRequestPlus` 解析全部目标，再把符号地址与剩余映像大小交给 `DCCRouteValidation`。校验失败调用 `disable(SignatureMismatch, ...)`。accelerator 三个 route 使用一次 `patcher.routeMultiple`；route 失败调用 `disable(RouteFailed, ...)`。成功后调用 `markRouteReady`。

- [ ] **步骤 5：运行 route、策略与集成测试**

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-route-green.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCRouteValidationTests.cpp -o "$test_dir/dcc-route-tests"
"$test_dir/dcc-route-tests"
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDiagnosticsPolicyTests.cpp -o "$test_dir/dcc-policy-tests"
"$test_dir/dcc-policy-tests"
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：全部退出码 0。

- [ ] **步骤 6：提交安全 route**

```sh
git add NootRX/X6000.hpp NootRX/X6000.cpp NootRX/X6000FB.hpp NootRX/X6000FB.cpp Tests/DCCDiagnosticsIntegrationTests.sh
git commit -m "fix: fail DCC diagnostics closed"
```

### 任务 5：文档、完整回归与推送

**文件：**
- 修改：`README.md`
- 修改：`docs/validation/2026-09-21-rx6750xt-dcc-diagnostic-runbook.md`

- [ ] **步骤 1：更新运行说明**

写明 `NootRX_DCCDiagSnapshot` 字典字段、route mask、失败代码、对数检查点以及 `Enabled=0` 时不得复现。

- [ ] **步骤 2：运行完整主机测试**

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-full-regression.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDiagnosticsPolicyTests.cpp -o "$test_dir/dcc-policy-tests"
"$test_dir/dcc-policy-tests"
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCRouteValidationTests.cpp -o "$test_dir/dcc-route-tests"
"$test_dir/dcc-route-tests"
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDisplayablePolicyTests.cpp -o "$test_dir/dcc-displayable-tests"
"$test_dir/dcc-displayable-tests"
clang++ -std=c++17 -Wall -Wextra -Werror Tests/PowerProfileTests.cpp -o "$test_dir/power-profile-tests"
"$test_dir/power-profile-tests"
for test_file in Tests/*Tests.sh; do sh "$test_file"; done
```

- [ ] **步骤 3：clean Release 构建并验证产物**

```sh
xcodebuild -project NootRX.xcodeproj -configuration Release -arch x86_64 clean build CODE_SIGNING_ALLOWED=NO
plutil -lint build/Release/NootRX.kext/Contents/Info.plist
sh Tests/DCCDiagnosticsIntegrationTests.sh NootRX/NootRX.cpp NootRX/X6000.cpp NootRX/X6000FB.cpp NootRX/DCCDiagnostics.cpp build/Release/NootRX.kext/Contents/MacOS/NootRX
```

预期：全部测试退出码 0，输出 `BUILD SUCCEEDED`，plist 为 OK。

- [ ] **步骤 4：提交文档并推送**

```sh
git add README.md docs/validation/2026-09-21-rx6750xt-dcc-diagnostic-runbook.md
git commit -m "docs: document low-overhead DCC diagnostics"
git push origin dcc-root-cause-diagnostics
```

- [ ] **步骤 5：停止在 EFI 安装关卡前**

报告构建产物位置和验证结果。只有用户再次确认目标 EFI 已挂载、路径正确且 boot-args 保持稳定基线后，才替换 kext。
