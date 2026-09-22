# Tahoe Navi22 GCHub donor selector 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 Tahoe + RX 6750 XT/Navi22 上将 Navi23 hardware 的 GCHub factory 选择性切换到 Apple 已有的 Navi21 `GCHub_10_3_0`，保留 MMHub_2_1_1 和全部现有固件、电源、DCC、Metal 路径。

**架构：** `X6000::processKext` 只在 Tahoe + Navi22 时解析 Navi21 的 `newGCHub()`，并把 Navi23 的同名 factory 路由到 wrapper。wrapper 调用 donor factory，返回 GCHub_10_3_0；Navi23 `newMMHub()` 不做任何修改。NootRXMain 负责发布实际 donor/class 到 IORegistry 和日志。

**技术栈：** C++17、Lilu KernelPatcher、IORegistry properties、POSIX shell guards、Xcode Release x86_64。

---

## 文件职责

- 创建：`Tests/GCHubPolicyTests.sh`——验证 Tahoe 条件、donor symbol、route、wrapper 和诊断属性契约。
- 修改：`NootRX/X6000.hpp`——声明 factory 类型、原始函数指针、wrapper 和注释。
- 修改：`NootRX/X6000.cpp`——安装 Tahoe + Navi22 的 GCHub route，调用 Navi21 donor factory。
- 修改：`NootRX/NootRX.hpp`——声明 GCHub 选择发布接口。
- 修改：`NootRX/NootRX.cpp`——写入 GCHub donor/class 属性并记录日志。
- 修改：`README.md`——记录补丁边界、实机观察指标和回退方式。

## 任务 1：先写 GCHub 失败测试

**文件：** `Tests/GCHubPolicyTests.sh`

- [x] **步骤 1：写入最小静态契约。**

```sh
#!/bin/sh
set -eu

x6000_source="${1:-NootRX/X6000.cpp}"
x6000_header="${2:-NootRX/X6000.hpp}"
nootrx_source="${3:-NootRX/NootRX.cpp}"

if ! rg -n 'AMDRadeonX6000_AMDNavi21Hardware8newGCHubEv' "$x6000_header" "$x6000_source"; then
    printf '%s\n' 'FAIL: Navi21 GCHub donor symbol is missing'
    exit 1
fi
if ! rg -U -q 'isNavi22\(\).*getKernelVersion\(\) == KernelVersion::Tahoe|KernelVersion::Tahoe.*isNavi22\(\)' "$x6000_source"; then
    printf '%s\n' 'FAIL: Tahoe + Navi22 GCHub gate is missing'
    exit 1
fi
if ! rg -U -q 'route\(patcher.*newGCHub|newGCHub.*route\(patcher' "$x6000_source"; then
    printf '%s\n' 'FAIL: Navi23 newGCHub route is missing'
    exit 1
fi
if ! rg -n 'GCHub_10_3_0|NootRX_GCHubDonor|NootRX_GCHubClass' "$x6000_source" "$nootrx_source"; then
    printf '%s\n' 'FAIL: GCHub donor diagnostics are missing'
    exit 1
fi
if rg -n 'newMMHub|AMDMMHub_2_1_0|AMDNavi21Hardware.*newMMHub' "$x6000_source"; then
    printf '%s\n' 'FAIL: GCHub patch must not change MMHub selection'
    exit 1
fi
printf '%s\n' 'PASS: Tahoe Navi22 selects the Navi21 GCHub donor without changing MMHub'
```

- [x] **步骤 2：运行测试确认旧代码正确失败。**

运行：

```sh
sh Tests/GCHubPolicyTests.sh NootRX/X6000.cpp NootRX/X6000.hpp NootRX/NootRX.cpp
```

预期：FAIL，原因是旧代码没有 Navi21 `newGCHub` donor route 和 GCHub diagnostics；如果因脚本语法或路径错误失败，先修正测试脚本。

- [x] **步骤 3：提交红灯测试。**

```sh
git add Tests/GCHubPolicyTests.sh
git commit -m "test: require Tahoe Navi22 GCHub donor selector"
```

## 任务 2：实现 GCHub route 和 wrapper

**文件：** `NootRX/X6000.hpp`、`NootRX/X6000.cpp`

- [x] **步骤 1：增加 factory 地址和 wrapper 声明。**

在 `X6000` 私有区域增加：

```cpp
mach_vm_address_t orgNewGCHub {0};
mach_vm_address_t donorNewGCHub {0};

// Routes Navi23's GCHub factory to the Navi21 GCHub_10_3_0 donor on Tahoe Navi22.
static void *wrapNewGCHub(void *that);
```

- [x] **步骤 2：在 Tahoe + Navi22 分支解析 donor 并安装 route。**

在 `X600::processKext` 处理 `AMDRadeonX6000` 的路径中，保留现有 HWInfo/DCC route，然后加入：

```cpp
if (NootRXMain::callback->attributes.isNavi22() && getKernelVersion() == KernelVersion::Tahoe) {
    SolveRequestPlus donorRequest {
        "__ZN32AMDRadeonX6000_AMDNavi21Hardware8newGCHubEv",
        this->donorNewGCHub};
    PANIC_COND(!donorRequest.solve(patcher, id, slide, size), "X6000",
        "Failed to solve Navi21 GCHub donor factory");

    RouteRequestPlus routeRequest {
        "__ZN32AMDRadeonX6000_AMDNavi23Hardware8newGCHubEv",
        wrapNewGCHub, this->orgNewGCHub};
    PANIC_COND(!routeRequest.route(patcher, id, slide, size), "X6000",
        "Failed to route Navi23 GCHub factory");

    NootRXMain::callback->publishGCHubSelection("Navi21", "AMDRadeonX6000_AMDGCHub_10_3_0");
    SYSLOG("X6000", "Tahoe Navi22 GCHub donor: Navi21 / GCHub_10_3_0; MMHub unchanged");
}
```

The factory symbols are present in Tahoe's `AMDRadeonX6000` binary; no byte-pattern fallback is added in this task.

- [x] **步骤 3：实现 wrapper。**

```cpp
void *X6000::wrapNewGCHub(void *that) {
    PANIC_COND(callback == nullptr || callback->donorNewGCHub == 0, "X6000",
        "Navi21 GCHub donor factory is unavailable");
    auto *hub = FunctionCast(wrapNewGCHub, callback->donorNewGCHub)(that);
    PANIC_COND(hub == nullptr, "X6000", "Navi21 GCHub donor factory returned null");
    SYSLOG("X6000", "Navi21 GCHub donor factory returned GCHub_10_3_0");
    return hub;
}
```

The wrapper never calls the original Navi23 factory on the selected path, so no `GCHub_10_3_4` object is created for Tahoe Navi22.

## 任务 3：发布诊断属性和文档

**文件：** `NootRX/NootRX.hpp`、`NootRX/NootRX.cpp`、`README.md`

- [x] **步骤 1：增加发布接口。**

在 `NootRX.hpp` 声明：

```cpp
// Publishes the selected GCHub donor and concrete class for post-reset diagnosis.
void publishGCHubSelection(const char *donorName, const char *hubClass);
```

在 `NootRX.cpp` 实现：

```cpp
// Publishes GCHub selection to IORegistry and records the exact donor/class in the system log.
void NootRXMain::publishGCHubSelection(const char *donorName, const char *hubClass) {
    auto *donor = OSString::withCString(donorName);
    auto *hub = OSString::withCString(hubClass);
    PANIC_COND(donor == nullptr || hub == nullptr, "NootRX", "Failed to allocate GCHub diagnostic strings");
    this->dGPU->setProperty("NootRX_GCHubDonor", donor);
    this->dGPU->setProperty("NootRX_GCHubClass", hub);
    donor->release();
    hub->release();
    SYSLOG("NootRX", "GCHub selection: donor=%s class=%s", donorName, hubClass);
}
```

- [x] **步骤 2：更新 README。**

记录 `NootRX_GCHubDonor=Navi21`、`NootRX_GCHubClass=AMDRadeonX6000_AMDGCHub_10_3_0`、MMHub 仍为 `AMDRadeonX6000_AMDMMHub_2_1_1`，并说明这只是 GCHub 假设验证，不宣称已经根治长期 reset。

- [x] **步骤 3：运行 GCHub 测试并提交。**

```sh
sh Tests/GCHubPolicyTests.sh NootRX/X6000.cpp NootRX/X6000.hpp NootRX/NootRX.cpp
git add NootRX/X6000.cpp NootRX/X6000.hpp NootRX/NootRX.cpp README.md
git commit -m "fix: select Navi21 GCHub for Tahoe Navi22"
```

## 任务 4：完整验证和 Release 构建

**文件：** `build/Release/NootRX.kext`

- [ ] **步骤 1：运行所有 shell 守卫。**

```sh
for script in Tests/*Tests.sh; do sh "$script"; done
```

- [ ] **步骤 2：运行现有 C++ 测试。**

```sh
for source in Tests/*Tests.cpp; do
    clang++ -std=c++17 -Wall -Wextra -Werror "$source" -o "/tmp/$(basename "$source" .cpp)"
    "/tmp/$(basename "$source" .cpp)"
done
```

- [ ] **步骤 3：构建 Release x86_64 kext。**

```sh
xcodebuild -project NootRX.xcodeproj -configuration Release -arch x86_64 clean build CODE_SIGNING_ALLOWED=NO
```

- [ ] **步骤 4：检查二进制包含新诊断字符串。**

```sh
strings -a build/Release/NootRX.kext/Contents/MacOS/NootRX | rg 'GCHub donor|NootRX_GCHubDonor|GCHub_10_3_0'
```

- [ ] **步骤 5：提交验证记录并推送。**

```sh
git status --short
git push origin dcc-root-cause-diagnostics
```

## 任务 5：用户挂载 EFI 后部署和实机验证

- [ ] **步骤 1：用户确认 EFI 已挂载后读取目标。**

检查 EFI 挂载点、`EFI/OC/Kexts/NootRX.kext` 修改时间和大小；先复制为 `.before-gchub-selector` 备份，不删除其他文件。

- [ ] **步骤 2：复制新 kext。**

仅替换 `EFI/OC/Kexts/NootRX.kext`，不修改 `config.plist`、boot-args 或四项电源 workaround。

- [ ] **步骤 3：重启后核对实际对象。**

```sh
ioreg -lw0 | rg 'NootRX_GCHubDonor|NootRX_GCHubClass|AMDRadeonX6000_AMDGCHub_10_3_0|AMDRadeonX6000_AMDMMHub_2_1_1'
```

预期同时看到 GCHub donor/class 属性、`GCHub_10_3_0` 计数为 1，以及 `MMHub_2_1_1` 计数为 1。

- [ ] **步骤 4：保持 Chrome 参数为空进行观察。**

验证 Chrome、IDEA、OpenDesign、Metal、VideoToolbox、显示器休眠/唤醒；若产生 `.gpuRestart`，记录首个 pending channel、应用、`dcc_en`、VM fault 和 RSMU 初始状态。

## 回退条件

若 kext 加载失败、Metal/OpenDesign 不可用、出现更早 GPU reset 或显示异常，恢复 EFI 的 `.before-gchub-selector` 备份；不要在同一版本上叠加 MMHub 改动。
