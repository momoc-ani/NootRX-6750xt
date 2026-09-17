# Tahoe Navi22 原生 capability 保留实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 仅在 macOS Tahoe + Navi22 上保留 Apple donor 的原生 DDI capability，避免 NootRX 通用表覆盖 Tahoe 的 `0x42000020`，同时保留 Metal、OpenDesign、VideoToolbox 与现有电源/GC 策略。

**架构：** 新增一个无状态、可主机单测的 capability 选择策略。X6000Framebuffer 与 HWLibs 在修改 donor 身份字段前保存原生指针，并共同调用该策略；最终选择值写入 GPU IORegistry 并记录启动日志。非 Tahoe 或非 Navi22 路径继续使用现有通用表。

**技术栈：** C++17、Lilu kernel extension、IOKit、shell/clang++ 主机测试、xcodebuild。

---

## 文件职责

- 创建 `NootRX/DDICapabilityPolicy.hpp`：只负责根据系统/GPU 条件选择 donor 或通用 capability 指针。
- 创建 `Tests/DDICapabilityPolicyTests.cpp`：覆盖 Tahoe/Navi22 与两个非目标组合。
- 修改 `NootRX/X6000FB.cpp`：在 framebuffer capability 表中应用选择策略。
- 修改 `NootRX/HWLibs.cpp`：在 HWLibs capability/init 表中应用相同策略。
- 修改 `NootRX/NootRX.hpp`、`NootRX/NootRX.cpp`：提供统一的 IORegistry 与日志记录方法。
- 修改 `README.md`：说明 Tahoe/Navi22 capability 保留补丁及验证属性。

### 任务 1：增加可测试的 capability 选择策略

**文件：**
- 创建：`Tests/DDICapabilityPolicyTests.cpp`
- 创建：`NootRX/DDICapabilityPolicy.hpp`

- [ ] **步骤 1：编写失败的策略测试**

测试使用两组 16 项 capability，donor 第 11 项为 `0x42000020`，通用表为 `0x42040028`：

```cpp
#include "../NootRX/DDICapabilityPolicy.hpp"
#include <cassert>
#include <cstdint>

static void testTahoeNavi22PreservesDonor() {
    const uint32_t donor[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x42000020};
    const uint32_t universal[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x42040028};
    const auto *selected = DDICapabilityPolicy::select(donor, universal, true, true);
    assert(selected == donor);
    assert(selected[DDICapabilityPolicy::FeatureCapsIndex] == 0x42000020);
}
```

同一测试程序还必须断言：非 Tahoe + Navi22、Tahoe + 非 Navi22 均返回通用表。

- [ ] **步骤 2：运行测试并验证红灯**

运行：

```sh
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DDICapabilityPolicyTests.cpp -o /private/tmp/nootrx-ddi-caps-tests
```

预期：编译失败，明确提示缺少 `NootRX/DDICapabilityPolicy.hpp`。

- [ ] **步骤 3：编写最小策略实现**

`NootRX/DDICapabilityPolicy.hpp` 内容保持无 IOKit 依赖：

```cpp
#pragma once
#include <cstdint>

namespace DDICapabilityPolicy {
constexpr uint32_t FeatureCapsIndex = 10;

// Returns the Apple donor table only for Tahoe Navi22; all existing paths retain the universal table.
inline const uint32_t *select(const uint32_t *donorCaps, const uint32_t *universalCaps, bool isTahoe,
    bool isNavi22) {
    return isTahoe && isNavi22 ? donorCaps : universalCaps;
}
}
```

- [ ] **步骤 4：运行测试并验证绿灯**

运行：

```sh
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DDICapabilityPolicyTests.cpp -o /private/tmp/nootrx-ddi-caps-tests
/private/tmp/nootrx-ddi-caps-tests
```

预期：两条命令退出码均为 0。

- [ ] **步骤 5：提交策略与单元测试**

```sh
git add NootRX/DDICapabilityPolicy.hpp Tests/DDICapabilityPolicyTests.cpp
git commit -m "test: define Tahoe Navi22 DDI caps policy"
```

### 任务 2：接入两个 Tahoe capability 表并发布诊断信息

**文件：**
- 修改：`NootRX/X6000FB.cpp:5,58-91`
- 修改：`NootRX/HWLibs.cpp:5,143-169`
- 修改：`NootRX/NootRX.hpp:55-69`
- 修改：`NootRX/NootRX.cpp:286` 附近
- 修改：`README.md:20-65`

- [ ] **步骤 1：为集成约束增加失败检查**

在 `Tests/DDICapabilityPolicyTests.sh` 中检查：

```sh
rg -n 'DDICapabilityPolicy::select' NootRX/X6000FB.cpp
rg -n 'DDICapabilityPolicy::select' NootRX/HWLibs.cpp
rg -n 'NootRX_DDICaps_X6000FB' NootRX/X6000FB.cpp
rg -n 'NootRX_DDICaps_HWLibs' NootRX/HWLibs.cpp
```

脚本还必须拒绝两个目标文件继续出现无条件的 `caps = ddiCapsNavi2Universal` 写入。

- [ ] **步骤 2：运行集成检查并验证红灯**

运行：`sh Tests/DDICapabilityPolicyTests.sh`

预期：FAIL，提示 X6000Framebuffer 尚未调用 capability 策略。

- [ ] **步骤 3：实现两个调用点**

两个路径都在覆盖 donor 身份字段前执行：

```cpp
const auto *selectedCaps = DDICapabilityPolicy::select(donorCaps, ddiCapsNavi2Universal,
    getKernelVersion() == KernelVersion::Tahoe, NootRXMain::callback->attributes.isNavi22());
```

X6000Framebuffer 的 `donorCaps` 为修改前的 `donorEntry->caps`；HWLibs 的 `donorCaps` 为修改前的 `orgCapsTable->caps`。`CAILAsicCapsInitTable` 必须继续使用 `orgCapsTable->caps`，保证同一路径指针一致。

- [ ] **步骤 4：实现统一诊断方法**

在 `NootRXMain` 中增加带用途注释的方法：

```cpp
// Publishes the selected DDI capability word for post-boot validation and logs its donor path.
void publishDDICapabilitySelection(const char *propertyName, UInt32 donorDeviceId, const UInt32 *caps);
```

实现将 `caps[DDICapabilityPolicy::FeatureCapsIndex]` 写入指定 GPU IORegistry 属性，并用 `SYSLOG` 记录 property、donor device ID 与值。两个调用点分别使用 `NootRX_DDICaps_X6000FB` 和 `NootRX_DDICaps_HWLibs`。

- [ ] **步骤 5：更新 fork 说明**

README 明确：该补丁仅影响 Tahoe + Navi22，保留 Apple donor 的 `0x42000020`，其他系统仍用通用表，且不改变 Metal/OpenDesign/VideoToolbox、PowerPlay 或 GC selector。

- [ ] **步骤 6：运行策略、集成和原有测试**

运行：

```sh
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DDICapabilityPolicyTests.cpp -o /private/tmp/nootrx-ddi-caps-tests
/private/tmp/nootrx-ddi-caps-tests
sh Tests/DDICapabilityPolicyTests.sh
sh Tests/GCVersionPolicyTests.sh
clang++ -std=c++17 -Wall -Wextra -Werror Tests/PowerProfileTests.cpp -o /private/tmp/nootrx-power-profile-tests
/private/tmp/nootrx-power-profile-tests
git diff --check
```

预期：全部退出码为 0。

- [ ] **步骤 7：提交实现**

只暂存本次文件，不暂存现有 SDMA 和用户文档：

```sh
git add NootRX/DDICapabilityPolicy.hpp Tests/DDICapabilityPolicyTests.cpp Tests/DDICapabilityPolicyTests.sh \
  NootRX/X6000FB.cpp NootRX/HWLibs.cpp NootRX/NootRX.hpp NootRX/NootRX.cpp README.md \
  docs/superpowers/plans/2026-09-17-tahoe-navi22-native-caps.md
git commit -m "fix: preserve Tahoe Navi22 native DDI caps"
```

### 任务 3：干净构建、远端推送与 EFI 部署

**文件：**
- 构建：`build/Release/NootRX.kext`
- 部署：`/Volumes/NO NAME/EFI/OC/Kexts/NootRX.kext`

- [ ] **步骤 1：保护现有 SDMA 工作区改动**

确认 `NootRX/Firmware/sdma_5_2_4_ucode.bin` 仍为唯一被跟踪的非本次改动。仅将该路径临时 stash，记录 stash 名称；不包含两个用户文档。

- [ ] **步骤 2：执行干净 Release 构建**

运行：

```sh
xcodebuild -project NootRX.xcodeproj -configuration Release -arch x86_64 clean build
```

预期：`** BUILD SUCCEEDED **`，退出码为 0。

- [ ] **步骤 3：恢复 SDMA 工作区改动**

立即恢复步骤 1 的精确 stash，并验证文件重新出现在 `git status --short`。若恢复失败，停止部署并保留 stash。

- [ ] **步骤 4：验证构建产物**

运行：

```sh
plutil -lint build/Release/NootRX.kext/Contents/Info.plist
codesign --verify --deep --strict build/Release/NootRX.kext
strings -a build/Release/NootRX.kext/Contents/MacOS/NootRX | rg 'NootRX_DDICaps_(X6000FB|HWLibs)'
sh Tests/DDICapabilityPolicyTests.sh NootRX/X6000FB.cpp NootRX/HWLibs.cpp build/Release/NootRX.kext/Contents/MacOS/NootRX
```

预期：plist 正常、签名验证退出码为 0、两个属性名都存在、集成检查通过。

- [ ] **步骤 5：推送远端**

运行：`git push origin master`

预期：远端 `master` 更新到实现提交。

- [ ] **步骤 6：备份并替换 EFI kext**

确认 `/Volumes/NO NAME/EFI/OC/Kexts/NootRX.kext` 存在。将旧 kext 复制到带日期时间的同目录备份，再用 `rsync` 替换为新 Release kext；不修改 `config.plist` 和其他 kext。

- [ ] **步骤 7：验证 EFI 副本一致性**

分别对 build 与 EFI 中的主二进制执行 `cmp`，对 EFI Info.plist 执行 `plutil -lint`，并读取 EFI kext 的 bundle version。预期 `cmp` 退出码为 0。

- [ ] **步骤 8：交付重启后验证命令**

重启后检查：

```sh
ioreg -l -w0 -p IOService | rg 'NootRX_DDICaps_X6000FB|NootRX_DDICaps_HWLibs'
```

两个值都应为十进制 `1107296288`（十六进制 `0x42000020`）。长期修复结论必须等待实际运行期间不再产生新的 `.gpuRestart`；本任务只声明补丁已构建并部署，不提前声明死机已经根治。
