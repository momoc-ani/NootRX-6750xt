# Tahoe RX 6750 XT GPUDCCDisplayable 开关实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为 macOS Tahoe + RX 6750 XT `0x73DF/0xC0` 增加独立的 `nootrx-gpu-dcc-displayable=0|1` 实验开关；默认不改写 accelerator personality，并把最终值与覆盖状态发布到 IORegistry 和启动日志。

**架构：** 新增一个无 IOKit 依赖的配置策略，集中处理目标平台门控、默认值和参数合法性。`NootRXMain` 在识别 GPU 后解析参数并发布诊断属性，`wrapAddDrivers` 只在 `AMDRadeonX6000` 的 Navi23 accelerator personality 与 `0x73DF1002` 同时匹配时改写顶层 `GPUDCCDisplayable`。PowerPlay、framebuffer、HWServices、DDI capability、GC、SDMA、Metal、OpenDesign 和 VideoToolbox 路径保持不变。

**技术栈：** C++17、Lilu boot-arg API、IOKit/libkern collections、shell/clang++ 主机测试、xcodebuild。

---

## 文件职责

- 创建 `NootRX/DCCDisplayablePolicy.hpp`：定义目标平台门控、稳定默认值以及 `0|1` 参数覆盖规则。
- 创建 `Tests/DCCDisplayablePolicyTests.cpp`：覆盖默认值、目标组合、显式 `0/1` 和非法值。
- 创建 `Tests/DCCDisplayableIntegrationTests.sh`：检查配置解析、accelerator personality 门控、IORegistry 属性、日志和构建产物字符串。
- 修改 `NootRX/NootRX.hpp`：保存 DCC 配置并声明配置与读取方法。
- 修改 `NootRX/NootRX.cpp`：解析 boot-arg、发布诊断信息并在注入前改写唯一目标 personality。
- 修改 `README.md`：说明实验边界、启用方法、验证命令和回滚方法。

### 任务 1：增加可主机测试的 DCC 配置策略

**文件：**
- 创建：`Tests/DCCDisplayablePolicyTests.cpp`
- 创建：`NootRX/DCCDisplayablePolicy.hpp`

- [x] **步骤 1：编写失败的策略测试**

创建 `Tests/DCCDisplayablePolicyTests.cpp`：

```cpp
#include "../NootRX/DCCDisplayablePolicy.hpp"

#include <cassert>
#include <cstdint>

// Verifies that installing the kext without the experiment argument preserves the injected XML.
static void testStableDefaultDoesNotOverride() {
    const auto policy = GPUDCCDisplayablePolicy::stable();
    assert(policy.value == 1);
    assert(!policy.overridden);
}

// Verifies that only Tahoe with the exact RX 6750 XT PCI identity enters the experiment path.
static void testTargetPlatformGate() {
    assert(GPUDCCDisplayablePolicy::isTarget(true, 0x73DF, 0xC0));
    assert(!GPUDCCDisplayablePolicy::isTarget(false, 0x73DF, 0xC0));
    assert(!GPUDCCDisplayablePolicy::isTarget(true, 0x73BF, 0xC0));
    assert(!GPUDCCDisplayablePolicy::isTarget(true, 0x73DF, 0xC1));
}

// Verifies that zero explicitly disables the displayable DCC property.
static void testDisableOverride() {
    auto policy = GPUDCCDisplayablePolicy::stable();
    assert(policy.overrideValue(0));
    assert(policy.value == 0);
    assert(policy.overridden);
}

// Verifies that one can explicitly retain the current displayable DCC property.
static void testEnableOverride() {
    auto policy = GPUDCCDisplayablePolicy::stable();
    assert(policy.overrideValue(1));
    assert(policy.value == 1);
    assert(policy.overridden);
}

// Verifies that unsupported values leave the stable configuration untouched.
static void testInvalidOverrideIsIgnored() {
    auto policy = GPUDCCDisplayablePolicy::stable();
    assert(!policy.overrideValue(2));
    assert(policy.value == 1);
    assert(!policy.overridden);
}

// Runs the dependency-free DCC displayable configuration tests on the build host.
int main() {
    testStableDefaultDoesNotOverride();
    testTargetPlatformGate();
    testDisableOverride();
    testEnableOverride();
    testInvalidOverrideIsIgnored();
    return 0;
}
```

该测试要抓住的破坏包括：默认状态意外开始改写 XML、非 Tahoe/非目标设备进入实验路径、非法值被当作有效覆盖。

- [x] **步骤 2：运行测试并验证红灯**

运行：

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-red.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDisplayablePolicyTests.cpp -o "$test_dir/dcc-displayable-tests"
```

预期：编译失败，明确提示缺少 `NootRX/DCCDisplayablePolicy.hpp`。

- [x] **步骤 3：编写最小策略实现**

创建 `NootRX/DCCDisplayablePolicy.hpp`：

```cpp
// Copyright © 2023-2026 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#pragma once

#include <stdint.h>

static constexpr const char *kGPUDCCDisplayableArg = "nootrx-gpu-dcc-displayable";

struct GPUDCCDisplayablePolicy {
    uint32_t value;
    bool overridden;

    // Returns the current injected personality value without requesting a rewrite.
    static constexpr GPUDCCDisplayablePolicy stable() {
        return {1, false};
    }

    // Returns whether the experiment is allowed for this operating system and PCI identity.
    static constexpr bool isTarget(bool isTahoe, uint32_t deviceId, uint32_t pciRevision) {
        return isTahoe && deviceId == 0x73DF && pciRevision == 0xC0;
    }

    // Accepts only boolean values and records that the personality must be explicitly rewritten.
    bool overrideValue(uint32_t requestedValue) {
        if (requestedValue > 1) { return false; }
        value = requestedValue;
        overridden = true;
        return true;
    }
};
```

- [x] **步骤 4：运行测试并验证绿灯**

运行：

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-green.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDisplayablePolicyTests.cpp -o "$test_dir/dcc-displayable-tests"
"$test_dir/dcc-displayable-tests"
```

预期：编译和测试进程均以退出码 0 结束。

- [x] **步骤 5：提交策略与单元测试**

```sh
git add NootRX/DCCDisplayablePolicy.hpp Tests/DCCDisplayablePolicyTests.cpp
git commit -m "test: define RX 6750 XT DCC displayable policy"
```

### 任务 2：接入 boot-arg、personality 改写与诊断信息

**文件：**
- 创建：`Tests/DCCDisplayableIntegrationTests.sh`
- 修改：`NootRX/NootRX.hpp`
- 修改：`NootRX/NootRX.cpp`
- 修改：`README.md`

- [x] **步骤 1：编写失败的集成守卫**

创建 `Tests/DCCDisplayableIntegrationTests.sh`。脚本先用当前 SDK 预处理实际 `NootRX.cpp`，排除注释和禁用分支造成的假阳性，再验证下列有效代码关系：

```sh
#!/bin/sh

set -eu

main_source="${1:-NootRX/NootRX.cpp}"
binary="${2:-}"
test_tmp_dir="$(mktemp -d "${TMPDIR:-/private/tmp}/nootrx-dcc-integration.XXXXXX")"
effective_source="$test_tmp_dir/NootRX.effective.cpp"

# Reports one DCC integration failure and stops the test immediately.
fail() {
    printf '%s\n' "FAIL: $1"
    exit 1
}

# Removes only the temporary directory created by this test invocation.
cleanup() {
    case "$test_tmp_dir" in
        "${TMPDIR:-/private/tmp}"/nootrx-dcc-integration.*) rm -rf -- "$test_tmp_dir" ;;
        *) printf '%s\n' "FAIL: refusing to remove unexpected temporary path $test_tmp_dir" >&2 ;;
    esac
}

trap cleanup EXIT HUP INT TERM

sdk_path="$(xcrun --sdk macosx --show-sdk-path)" || fail "failed to locate the macOS SDK"
clang -E -P -fdirectives-only -x c++ -std=c++17 -nostdinc \
    -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
    -I . -I NootRX -I Lilu/Lilu -I MacKernelSDK/Headers \
    -I "$sdk_path/System/Library/Frameworks/Kernel.framework/PrivateHeaders" \
    -isysroot "$sdk_path" "$main_source" -o "$effective_source" ||
    fail "failed to preprocess $main_source"

rg -F 'GPUDCCDisplayablePolicy::isTarget(' "$effective_source" >/dev/null ||
    fail "Tahoe RX 6750 XT target gate is missing"
rg -F 'lilu_get_boot_args(kGPUDCCDisplayableArg' "$effective_source" >/dev/null ||
    fail "DCC displayable boot argument is not parsed"
rg -F 'AMDRadeonX6000_AMDNavi23GraphicsAccelerator' "$effective_source" >/dev/null ||
    fail "Navi23 accelerator personality gate is missing"
rg -F '0x73DF1002' "$effective_source" >/dev/null ||
    fail "RX 6750 XT IOPCIMatch gate is missing"
rg -F 'GPUDCCDisplayable' "$effective_source" >/dev/null ||
    fail "GPUDCCDisplayable personality write is missing"
rg -F 'NootRX_GPUDCCDisplayable' "$effective_source" >/dev/null ||
    fail "effective DCC IORegistry property is missing"
rg -F 'NootRX_GPUDCCDisplayableOverride' "$effective_source" >/dev/null ||
    fail "DCC override IORegistry property is missing"
rg -F 'os=Tahoe' "$effective_source" >/dev/null ||
    fail "DCC startup diagnostic log is missing"

if [ -n "$binary" ]; then
    strings -a "$binary" | rg -F 'nootrx-gpu-dcc-displayable' >/dev/null ||
        fail "built kext is missing the DCC displayable boot argument"
    strings -a "$binary" | rg -F 'NootRX_GPUDCCDisplayableOverride' >/dev/null ||
        fail "built kext is missing the DCC override property"
fi

printf '%s\n' "PASS: Tahoe RX 6750 XT DCC displayable override is wired to the accelerator personality"
```

该守卫要抓住的破坏包括：配置对象存在但未接入启动链路、写错驱动分支、缺少 Navi23/PCI 双重匹配、诊断属性只存在于注释中、最终二进制未包含参数或属性。

- [x] **步骤 2：运行集成守卫并验证红灯**

运行：

```sh
sh Tests/DCCDisplayableIntegrationTests.sh
```

预期：FAIL，提示 Tahoe RX 6750 XT target gate 尚未接入。

- [x] **步骤 3：在主类中保存并解析配置**

在 `NootRX/NootRX.hpp` 引入策略头文件，增加带用途注释的方法和字段：

```cpp
// Reads the Tahoe RX 6750 XT DCC displayable experiment and publishes its effective state.
void configureDCCDisplayable();

// Returns the effective DCC displayable policy used by accelerator personality injection.
const GPUDCCDisplayablePolicy &getDCCDisplayablePolicy() const { return this->dccDisplayablePolicy; }

GPUDCCDisplayablePolicy dccDisplayablePolicy {GPUDCCDisplayablePolicy::stable()};
```

在 `processPatcher()` 完成设备 ID、revision 和 Navi 属性识别后调用 `configureDCCDisplayable()`。实现必须先调用：

```cpp
GPUDCCDisplayablePolicy::isTarget(
    getKernelVersion() == KernelVersion::Tahoe, this->deviceId, this->pciRevision)
```

非目标组合立即返回。目标组合只解析 `kGPUDCCDisplayableArg`；`0` 或 `1` 调用 `overrideValue()`，其他数值记录忽略日志。随后始终向 GPU PCI 服务写入：

```text
NootRX_GPUDCCDisplayable
NootRX_GPUDCCDisplayableOverride
```

并记录包含 `device=0x73DF pciRev=0xC0 os=Tahoe value=<0|1> override=<yes|no>` 的启动日志。

- [x] **步骤 4：只改写目标 accelerator personality**

在 `NootRX/NootRX.cpp` 增加带用途注释的 `applyGPUDCCDisplayableOverride()`。方法按以下顺序返回 `false`：配置未覆盖、`IOClass` 不是 `AMDRadeonX6000_AMDNavi23GraphicsAccelerator`、`IOPCIMatch` 不包含 `0x73DF1002`。全部匹配时执行：

```cpp
driver->setObject("GPUDCCDisplayable",
    policy.value != 0 ? kOSBooleanTrue : kOSBooleanFalse)
```

`wrapAddDrivers()` 只在 `identifierIndex == 0`（`com.apple.kext.AMDRadeonX6000`）时调用该 helper；成功后记录最终值。不得修改 framebuffer (`identifierIndex == 2`) 的 PowerPlay helper 及其他注入数组。

- [x] **步骤 5：更新 fork 说明和实机验证边界**

在 README 的 Chromium 闪烁章节后增加 `GPUDCCDisplayable experiment` 小节，明确：

```text
nootrx-gpu-dcc-displayable=0
```

仅用于 Tahoe + `0x73DF/0xC0` 单变量验证；未传参数时 XML 保持 `true` 且不发生 personality 改写；`=1` 是显式 true 对照；其他值被忽略。给出以下验证命令：

```sh
ioreg -l -w0 -p IOService | rg 'NootRX_GPUDCCDisplayable'
log show --last boot --style compact --predicate 'eventMessage CONTAINS[c] "DCC displayable"'
```

回滚方式为删除 boot-arg。文档不得声称它已经修复闪烁、GPU Reset、GFX hang 或 WindowServer watchdog，也不得暗示 Metal、OpenDesign 或 VideoToolbox 被关闭。

- [x] **步骤 6：运行新增测试和现有回归测试**

运行：

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-regression.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDisplayablePolicyTests.cpp -o "$test_dir/dcc-displayable-tests"
"$test_dir/dcc-displayable-tests"
sh Tests/DCCDisplayableIntegrationTests.sh
clang++ -std=c++17 -Wall -Wextra -Werror Tests/PowerProfileTests.cpp -o "$test_dir/power-profile-tests"
"$test_dir/power-profile-tests"
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DDICapabilityPolicyTests.cpp -o "$test_dir/ddi-capability-tests"
"$test_dir/ddi-capability-tests"
sh Tests/DDICapabilityPolicyTests.sh
sh Tests/DDICapabilityIntegrationGuardTests.sh
sh Tests/GCVersionPolicyTests.sh
git diff --check
```

预期：所有命令退出码为 0；现有 PowerPlay、DDI capability 与 GC 策略测试无回归。

- [x] **步骤 7：提交接入实现与文档**

```sh
git add NootRX/NootRX.hpp NootRX/NootRX.cpp Tests/DCCDisplayableIntegrationTests.sh README.md
git commit -m "feat: add Tahoe RX 6750 XT DCC displayable override"
```

### 任务 3：Release 构建、产物验证与远端集成

**文件：**
- 构建：`build/Release/NootRX.kext`
- 集成：主工作区 `master`

- [x] **步骤 1：执行干净 Release x86_64 构建**

运行：

```sh
xcodebuild -project NootRX.xcodeproj -configuration Release -arch x86_64 clean build
```

预期：输出包含 `** BUILD SUCCEEDED **`，退出码为 0。

- [x] **步骤 2：验证构建产物包含实际接线**

运行：

```sh
plutil -lint build/Release/NootRX.kext/Contents/Info.plist
codesign --verify --deep --strict build/Release/NootRX.kext
sh Tests/DCCDisplayableIntegrationTests.sh NootRX/NootRX.cpp build/Release/NootRX.kext/Contents/MacOS/NootRX
strings -a build/Release/NootRX.kext/Contents/MacOS/NootRX | rg 'nootrx-gpu-dcc-displayable|NootRX_GPUDCCDisplayable'
```

预期：plist、签名与集成守卫全部通过，二进制包含 boot-arg 和两个 IORegistry 属性名。

- [x] **步骤 3：重新运行完整测试与工作区检查**

运行任务 2 步骤 6 的完整命令，并额外运行：

```sh
git status --short
git diff --check
```

预期：只出现计划内文件或无未提交改动；不得出现 SDMA 固件和用户草稿文档。

- [ ] **步骤 4：快进集成到主工作区并推送 fork**

在确认主工作区仍只保留既有的固件和用户文档改动后，运行：

```sh
git -C /Users/momoc/Desktop/my_project/NootRX merge --ff-only feature/tahoe-rx6750xt-dcc-displayable
export https_proxy=http://127.0.0.1:7897
export http_proxy=http://127.0.0.1:7897
export all_proxy=socks5://127.0.0.1:7897
git -C /Users/momoc/Desktop/my_project/NootRX push origin master
```

预期：`master` 快进到功能分支，远端 fork 更新；主工作区原有 `sdma_5_2_4_ucode.bin`、SQL 草稿和黑苹果问题笔记仍保持原状态且不进入提交。

- [ ] **步骤 5：交付实机测试入口**

交付时明确：代码与构建验证不等于闪烁根因已确认。首次实机验证只增加 `nootrx-gpu-dcc-displayable=0`，重启后先核对两个 IORegistry 属性，再恢复 Chrome GPU Rasterization 默认值复现同一 Holopix 页面；出现新的 GPU Reset、GFX hang 或 WindowServer watchdog 时删除参数回滚。
