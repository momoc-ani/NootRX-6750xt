# Tahoe DCC OS build gate 修正实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 将 Tahoe RX 6750 XT DCC 诊断的精确 build 数据源从早期全局 `osversion` 改为 IORegistry 根节点的 `OS Build Version`，属性缺失时保持关闭。

**架构：** `NootRX.cpp` 增加一个只负责读取 IORegistry 根属性的内部函数；`processPatcher()` 只读取一次字符串，并把同一个值传给观察属性和实际 target gate。纯 C++ `DCCDiagnosticsPolicy`、route 安装、签名校验及 GPU 行为不变。

**技术栈：** C++17、IOKit `IORegistryEntry`、libkern `OSString`、shell 集成守卫、clang++ 主机测试、xcodebuild。

---

## 文件职责

- 修改 `Tests/DCCDiagnosticsIntegrationTests.sh`：防止 build gate 回退到全局 `osversion`，并验证观察值与 target gate 共用 IORegistry build。
- 修改 `NootRX/NootRX.cpp`：读取 IORegistry 根节点的 `kOSBuildVersionKey`，执行 fail-closed 门控并记录拒绝原因。
- 修改 `README.md`：说明 build gate 的实际数据源。
- 修改 `docs/validation/2026-09-21-dcc-diagnostics-two-feature-progress.md`：记录红绿测试、构建结果和下一次启动验证条件。

### 任务 1：以 TDD 修正 DCC build 数据源

**文件：**
- 修改：`Tests/DCCDiagnosticsIntegrationTests.sh:50-70`
- 修改：`NootRX/NootRX.cpp:5-20,175-195`

- [x] **步骤 1：增加会抓住旧数据源的失败守卫**

在现有 `NootRX_DCCDiagOSBuildMatch` 检查后加入：

```sh
rg -F 'IORegistryEntry::getRegistryRoot()' "$main_effective" >/dev/null ||
    fail "DCC diagnostics do not read the IORegistry root"
rg -F 'root->getProperty(kOSBuildVersionKey)' "$main_effective" >/dev/null ||
    fail "DCC diagnostics do not read the root OS build property"
rg -F 'const auto *osBuild = getOSBuildVersion();' "$main_effective" >/dev/null ||
    fail "DCC diagnostics do not capture one runtime OS build value"
rg -F 'DCCDiagnosticsPolicy::observeGate(osBuild, this->dccDiagnosticsRequested)' \
    "$main_effective" >/dev/null || fail "DCC gate observation does not use the IORegistry build"
rg -U -P -q 'DCCDiagnosticsPolicy::isTarget\((?s:.*?)osBuild, this->deviceId' \
    "$main_effective" || fail "DCC target gate does not reuse the IORegistry build"
if rg -F 'extern "C" char osversion[]' "$main_effective" >/dev/null ||
    rg -F 'observeGate(osversion' "$main_effective" >/dev/null ||
    rg -U -P -q 'DCCDiagnosticsPolicy::isTarget\((?s:.*?)osversion' "$main_effective"; then
    fail "DCC diagnostics still depend on the global osversion input"
fi
```

删除守卫中要求 `observeGate(osversion, ...)` 和 `isTarget(..., osversion, ...)` 的旧断言。

- [x] **步骤 2：运行守卫确认正确红灯**

运行：

```sh
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：退出码非 0，首个新增失败为：

```text
FAIL: DCC diagnostics do not read the IORegistry root
```

- [x] **步骤 3：添加最小 IORegistry build reader**

在 `NootRX/NootRX.cpp` 增加头文件：

```cpp
#include <IOKit/IOKitKeys.h>
#include <libkern/c++/OSString.h>
```

删除：

```cpp
extern "C" char osversion[];
```

在静态路径常量之前增加带功能注释的内部函数：

```cpp
// Returns the OS build published by the IORegistry root, or nullptr when unavailable.
static const char *getOSBuildVersion() {
    auto *root = IORegistryEntry::getRegistryRoot();
    if (root == nullptr) { return nullptr; }

    auto *build = OSDynamicCast(OSString, root->getProperty(kOSBuildVersionKey));
    return build != nullptr ? build->getCStringNoCopy() : nullptr;
}
```

在 `processPatcher()` 的 DCC 配置处只读取一次：

```cpp
const auto *osBuild = getOSBuildVersion();
const auto dccGateObservation =
    DCCDiagnosticsPolicy::observeGate(osBuild, this->dccDiagnosticsRequested);
```

观察属性发布后加入故障排查日志：

```cpp
SYSLOG_COND(this->dccDiagnosticsRequested && !dccGateObservation.osBuildMatch, "NootRX",
    "DCC diagnostics disabled: unsupported or unavailable OS build %s",
    osBuild != nullptr ? osBuild : "(missing)");
```

实际 target gate 使用同一个 `osBuild`：

```cpp
this->dccDiagnostics.configure(this->dGPU,
    DCCDiagnosticsPolicy::isTarget(getKernelVersion() == KernelVersion::Tahoe, osBuild, this->deviceId,
        this->pciRevision, this->dccDiagnosticsRequested));
```

- [x] **步骤 4：运行目标测试确认绿灯**

运行：

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-build-gate.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDiagnosticsPolicyTests.cpp -o "$test_dir/dcc-policy-tests"
"$test_dir/dcc-policy-tests"
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：编译、策略测试和集成守卫退出码均为 0，无警告。

- [x] **步骤 5：提交单一实现**

```sh
git add NootRX/NootRX.cpp Tests/DCCDiagnosticsIntegrationTests.sh
git commit -m "fix: read DCC build gate from IORegistry"
```

### 任务 2：完整回归、Release 构建与进度记录

**文件：**
- 修改：`README.md:238-244`
- 修改：`docs/validation/2026-09-21-dcc-diagnostics-two-feature-progress.md`
- 已创建：`docs/superpowers/plans/2026-09-22-dcc-os-build-gate.md`

- [x] **步骤 1：更新用户文档与验证进度**

将 README 中 `NootRX_DCCDiagOSBuildMatch` 的说明从内核 `osversion` 改为：仅当
IORegistry 根节点 `OS Build Version` 精确等于 `25E253` 时为 `1`，属性缺失时为
`0`。

在验证进度文档追加 P5.2，记录：

```text
数据源 = IORegistry root / OS Build Version
缺失策略 = fail closed
global osversion fallback = none
```

并保留下次启动通过条件：`Requested=1`、`OSBuildMatch=1`、`Enabled=1`、
`RouteMask=3`、`FailureCode=0`。

- [x] **步骤 2：运行全部 C++ 主机测试**

运行：

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-build-gate-regression.XXXXXX)
for source in Tests/*Tests.cpp; do
    binary="$test_dir/$(basename "$source" .cpp)"
    clang++ -std=c++17 -Wall -Wextra -Werror "$source" -o "$binary"
    "$binary"
done
```

预期：6 个测试程序全部编译并退出 0。

- [x] **步骤 3：运行全部 shell 集成守卫**

运行：

```sh
for script in Tests/*Tests.sh; do
    sh "$script"
done
```

预期：5 个脚本全部退出 0。

- [x] **步骤 4：clean Release x86_64 构建**

运行：

```sh
xcodebuild -project NootRX.xcodeproj -configuration Release -arch x86_64 clean build CODE_SIGNING_ALLOWED=NO
```

预期：退出码为 0，输出包含 `** BUILD SUCCEEDED **`。

- [x] **步骤 5：验证最终产物**

运行：

```sh
plutil -lint build/Release/NootRX.kext/Contents/Info.plist
sh Tests/DCCDiagnosticsIntegrationTests.sh NootRX/NootRX.cpp NootRX/X6000.cpp NootRX/X6000FB.cpp NootRX/DCCDiagnostics.cpp build/Release/NootRX.kext/Contents/MacOS/NootRX
strings -a build/Release/NootRX.kext/Contents/MacOS/NootRX | rg 'OS Build Version|NootRX_DCCDiagOSBuildMatch|NootRX_DCCDiagEnabled|DCCDIAG'
git diff --check
```

预期：plist 为 OK；二进制集成守卫、必需标记和 diff 检查全部通过。

- [x] **步骤 6：提交文档并推送分支**

```sh
git add README.md docs/validation/2026-09-21-dcc-diagnostics-two-feature-progress.md docs/superpowers/plans/2026-09-22-dcc-os-build-gate.md
git commit -m "docs: record DCC build gate fix"
git push origin dcc-root-cause-diagnostics
```

推送后保持 EFI 不变；只有用户明确要求部署时，才替换实际启动分区中的
`NootRX.kext`。
