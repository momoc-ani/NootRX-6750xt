# Tahoe RX 6750 XT Displayable DCC 根因诊断实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在不改变 Displayable DCC 决策和 metadata 的前提下，为 Tahoe + RX 6750 XT 建立限频、可关联的 accelerator／AddrLib2／Framebuffer 诊断链，取得一次 DCC 开启复现所需的根因证据。

**架构：** 使用独立的纯 C++ ABI/限频策略保存 Apple DCC 结构和精确观察键，再由一个 kernel-only `DCCDiagnostics` 服务负责日志与 IORegistry 快照。`X6000` 只路由 AddrLib 身份、scanout 判定与 DCC metadata 计算；`X6000FB` 只路由 request type `0x1A` 的 capability 往返。全部路由仅在 Tahoe + `0x73DF/0xC0` + `-NRXPowerDiag` 下安装，wrapper 必须先/后调用原函数但不得改写任何输入、输出或返回值。

**技术栈：** C++17、Lilu `RouteRequestPlus`、IOKit、Apple x86_64 kext ABI、AMD AddrLib2 v1 结构、shell/clang++ 主机测试、xcodebuild。

**范围边界：** 本计划交付“可运行的诊断版本 + 一次受控复现的数据”。根因字段被证据确认后，再为唯一修复点编写下一份实现计划；本计划不预先猜测并修改 capability、ASIC family、metadata 或 scanout 返回值。

---

## 已确认的 Tahoe 25E253 ABI 事实

- `AMDRadeonX6000_AMDAccelResourceAddr2::shouldAllocScanoutDcc(unsigned int, unsigned int, unsigned int, unsigned int)`：
  - `arg0/arg1` 被转换为 float 后作为宽、高发给 Framebuffer；
  - `arg2` 必须非零，并原样写入 0x1A 请求；
  - `arg3` 先经格式转换虚函数处理，再写入 0x1A 请求；
  - 只有全局 `GPUDCCDisplayable` 位、资源 displayable 位、0x1A 返回成功及 capability byte 0 同时为真时才返回 true。
- accelerator 发给 Framebuffer 的 0x1A 输入大小为 `0x20`，版本字段为 `1`；输出大小为 `0x10`，版本字段为 `1`。
- `AMDRadeonX6000_AMDHWAlignManager2::getDccInfo2(...)` 的 Apple AddrLib2 输入大小为 `0x34`，输出大小为 `0x48`。
- `AMDHWAlignManager2::init(...)` 构造 `ADDR_CREATE_INPUT` 时，通过硬件接口虚表读取：
  - `0x140`：chip engine；
  - `0x1B8`：chip family；
  - `0x1A8`：chip revision。
- Framebuffer 的 `callPlatformFunctionFromDrvr` switch 中，request `0x1A` 进入 DCC capability 分支；该分支校验两个版本字段均为 `1`，并向输出写入 3 个 capability byte 和 2 个 32-bit 字段。

这些事实只适用于当前实机的 macOS 26.4.1 build 25E253。运行时必须再次验证结构版本和大小；不匹配时只记录 ABI mismatch，不能解引用后续字段。

## 文件职责

- 创建 `docs/research/2026-09-21-tahoe-25e253-dcc-abi.md`：保存符号、反汇编边界、字段偏移和未命名字段，明确“事实”与“推断”。
- 创建 `NootRX/DCCDiagnosticsPolicy.hpp`：定义 Apple ABI POD、观察键、无哈希的固定容量去重器和目标平台门控。
- 创建 `NootRX/DCCDiagnostics.hpp`：声明 kernel-only 诊断服务和四类记录入口。
- 创建 `NootRX/DCCDiagnostics.cpp`：实现 IORegistry 快照、统一日志、锁和限频逻辑。
- 创建 `Tests/DCCDiagnosticsPolicyTests.cpp`：验证 ABI 大小/偏移、目标门控、去重、变化、失败和容量行为。
- 创建 `Tests/DCCDiagnosticsIntegrationTests.sh`：验证实际有效代码中的目标门控、三个 accelerator route、一个 Framebuffer route、只读 wrapper 和构建产物字符串。
- 修改 `NootRX/NootRX.hpp`：保存诊断服务并提供受控访问器。
- 修改 `NootRX/NootRX.cpp`：设备识别后启用或禁用 DCC 诊断并发布启动状态。
- 修改 `NootRX/X6000.hpp`、`NootRX/X6000.cpp`：路由 AddrLib 身份、scanout DCC 判定和 `getDccInfo2`。
- 修改 `NootRX/X6000FB.hpp`、`NootRX/X6000FB.cpp`：路由并记录 request `0x1A` 的输入、输出和返回码。
- 修改 `NootRX.xcodeproj/project.pbxproj`：把 `DCCDiagnostics.cpp` 加入唯一的 Sources phase。
- 修改 `README.md`：说明诊断版本不修补 DCC、启用条件、日志字段和回滚方式。
- 创建 `docs/validation/2026-09-21-rx6750xt-dcc-diagnostic-runbook.md`：固定一次受控复现和证据归类步骤。

### 任务 1：固化 Tahoe DCC ABI 与结构守卫

**文件：**
- 创建：`docs/research/2026-09-21-tahoe-25e253-dcc-abi.md`
- 创建：`NootRX/DCCDiagnosticsPolicy.hpp`
- 创建：`Tests/DCCDiagnosticsPolicyTests.cpp`

- [ ] **步骤 1：编写失败的 ABI 布局测试**

创建 `Tests/DCCDiagnosticsPolicyTests.cpp`，先只加入 ABI 测试：

```cpp
#include "../NootRX/DCCDiagnosticsPolicy.hpp"

#include <cassert>
#include <cstddef>

// Verifies the exact AddrLib2 v1 input layout used by Tahoe build 25E253.
static void testAddrLibDccInputLayout() {
    static_assert(sizeof(AppleAddr2ComputeDccInfoInputV1) == 0x34);
    static_assert(offsetof(AppleAddr2ComputeDccInfoInputV1, swizzleMode) == 0x10);
    static_assert(offsetof(AppleAddr2ComputeDccInfoInputV1, bpp) == 0x14);
    static_assert(offsetof(AppleAddr2ComputeDccInfoInputV1, unalignedWidth) == 0x18);
    static_assert(offsetof(AppleAddr2ComputeDccInfoInputV1, firstMipIdInTail) == 0x30);
}

// Verifies the exact AddrLib2 v1 output layout used by Tahoe build 25E253.
static void testAddrLibDccOutputLayout() {
    static_assert(sizeof(AppleAddr2ComputeDccInfoOutputV1) == 0x48);
    static_assert(offsetof(AppleAddr2ComputeDccInfoOutputV1, dccRamBaseAlign) == 0x04);
    static_assert(offsetof(AppleAddr2ComputeDccInfoOutputV1, pitch) == 0x0C);
    static_assert(offsetof(AppleAddr2ComputeDccInfoOutputV1, metaBlkSize) == 0x30);
    static_assert(offsetof(AppleAddr2ComputeDccInfoOutputV1, pMipInfo) == 0x40);
}

// Verifies the external accelerator-to-framebuffer DCC request layouts.
static void testFramebufferDccRequestLayout() {
    static_assert(sizeof(AppleDccCapsParametersV1) == 0x20);
    static_assert(offsetof(AppleDccCapsParametersV1, width) == 0x10);
    static_assert(sizeof(AppleDccCapabilitiesV1) == 0x10);
    static_assert(offsetof(AppleDccCapabilitiesV1, capability0) == 0x04);
    static_assert(offsetof(AppleDccCapabilitiesV1, field0) == 0x08);
}

// Runs all dependency-free DCC diagnostic policy tests.
int main() {
    testAddrLibDccInputLayout();
    testAddrLibDccOutputLayout();
    testFramebufferDccRequestLayout();
    return 0;
}
```

- [ ] **步骤 2：运行测试并确认红灯**

运行：

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-diag-abi-red.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDiagnosticsPolicyTests.cpp -o "$test_dir/dcc-diag-tests"
```

预期：编译失败，明确提示缺少 `NootRX/DCCDiagnosticsPolicy.hpp`。

- [ ] **步骤 3：实现带大小与偏移守卫的 Apple ABI POD**

创建 `NootRX/DCCDiagnosticsPolicy.hpp`。所有未能由反汇编证明语义的字段必须使用中性名称，不得伪造含义：

```cpp
#pragma once

#include <stddef.h>
#include <stdint.h>

// Mirrors Tahoe 25E253's AddrLib2 DCC input. The size field must equal 0x34 before use.
struct AppleAddr2ComputeDccInfoInputV1 {
    uint32_t size;
    uint32_t dccKeyFlags;
    uint32_t colorFlags;
    uint32_t resourceType;
    uint32_t swizzleMode;
    uint32_t bpp;
    uint32_t unalignedWidth;
    uint32_t unalignedHeight;
    uint32_t numSlices;
    uint32_t numFrags;
    uint32_t numMipLevels;
    uint32_t dataSurfaceSize;
    uint32_t firstMipIdInTail;
};

// Mirrors Tahoe 25E253's AddrLib2 DCC output. The trailing pointer is observed but never dereferenced.
struct AppleAddr2ComputeDccInfoOutputV1 {
    uint32_t size;
    uint32_t dccRamBaseAlign;
    uint32_t dccRamSize;
    uint32_t pitch;
    uint32_t height;
    uint32_t depth;
    uint32_t compressBlkWidth;
    uint32_t compressBlkHeight;
    uint32_t compressBlkDepth;
    uint32_t metaBlkWidth;
    uint32_t metaBlkHeight;
    uint32_t metaBlkDepth;
    uint32_t metaBlkSize;
    uint32_t metaBlkNumPerSlice;
    uint32_t dccRamSliceSize;
    uint32_t reserved;
    void *pMipInfo;
};

// Mirrors the version-1 request sent by shouldAllocScanoutDcc to request type 0x1A.
struct AppleDccCapsParametersV1 {
    uint32_t version;
    uint32_t pixelFormat;
    uint64_t reserved;
    float width;
    float height;
    uint32_t candidateFlags;
    uint32_t field0;
};

// Mirrors the version-1 response returned by Framebuffer request type 0x1A.
struct AppleDccCapabilitiesV1 {
    uint32_t version;
    uint8_t capability0;
    uint8_t capability1;
    uint8_t capability2;
    uint8_t reserved;
    uint32_t field0;
    uint32_t field1;
};

static_assert(sizeof(AppleAddr2ComputeDccInfoInputV1) == 0x34);
static_assert(sizeof(AppleAddr2ComputeDccInfoOutputV1) == 0x48);
static_assert(sizeof(AppleDccCapsParametersV1) == 0x20);
static_assert(sizeof(AppleDccCapabilitiesV1) == 0x10);
```

- [ ] **步骤 4：记录可复查的静态证据**

创建 `docs/research/2026-09-21-tahoe-25e253-dcc-abi.md`，写入：

- 操作系统与 build：macOS 26.4.1 `25E253`；
- 四个路由符号的完整 mangled name；
- `shouldAllocScanoutDcc` 的 `0x2022e..0x2031b` 控制流结论；
- `getDccInfo2` wrapper 的 `0x5f87c..0x5f8cb` 结论；
- `AMDHWAlignManager2::init` 的虚表偏移；
- Framebuffer request `0x1A` 对应 `callPlatformFunctionFromDrvr+0x13e` 分支；
- ABI 字段表，其中宽、高、版本、大小标为“已确认”，三个 capability byte 与两个返回字段标为“语义未命名”；
- Mesa AddrLib 只作为结构字段顺序参考，不能证明 Apple fork 的每个字段语义完全一致。

复查命令：

```sh
nm -nm /System/Library/Extensions/AMDRadeonX6000.kext/Contents/MacOS/AMDRadeonX6000 | rg 'shouldAllocScanoutDcc|getDccInfo2|AMDHWAlignManager24init'
objdump -d --no-show-raw-insn --start-address=0x2022e --stop-address=0x2031c /System/Library/Extensions/AMDRadeonX6000.kext/Contents/MacOS/AMDRadeonX6000
objdump -d --no-show-raw-insn --start-address=0x4f23e --stop-address=0x4f810 /System/Library/Extensions/AMDRadeonX6000Framebuffer.kext/Contents/MacOS/AMDRadeonX6000Framebuffer
```

- [ ] **步骤 5：运行 ABI 测试并确认绿灯**

运行：

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-diag-abi-green.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDiagnosticsPolicyTests.cpp -o "$test_dir/dcc-diag-tests"
"$test_dir/dcc-diag-tests"
```

预期：编译和执行均为退出码 0。

- [ ] **步骤 6：提交 ABI 证据与守卫**

```sh
git add NootRX/DCCDiagnosticsPolicy.hpp Tests/DCCDiagnosticsPolicyTests.cpp docs/research/2026-09-21-tahoe-25e253-dcc-abi.md
git commit -m "test: define Tahoe DCC diagnostic ABI"
```

### 任务 2：实现无哈希、固定容量的诊断限频策略

**文件：**
- 修改：`NootRX/DCCDiagnosticsPolicy.hpp`
- 修改：`Tests/DCCDiagnosticsPolicyTests.cpp`

- [ ] **步骤 1：增加失败的门控与限频测试**

向 `Tests/DCCDiagnosticsPolicyTests.cpp` 增加：

```cpp
// Verifies that diagnostics cannot affect other OS or GPU combinations.
static void testDiagnosticTargetGate() {
    assert(DCCDiagnosticsPolicy::isTarget(true, 0x73DF, 0xC0, true));
    assert(!DCCDiagnosticsPolicy::isTarget(false, 0x73DF, 0xC0, true));
    assert(!DCCDiagnosticsPolicy::isTarget(true, 0x73FF, 0xC0, true));
    assert(!DCCDiagnosticsPolicy::isTarget(true, 0x73DF, 0xC1, true));
    assert(!DCCDiagnosticsPolicy::isTarget(true, 0x73DF, 0xC0, false));
}

// Verifies that an exact duplicate is counted but not logged twice.
static void testExactDuplicateIsSuppressed() {
    DCCObservationCache cache;
    const DCCObservationKey key {DCCDiagnosticStage::ScanoutDecision, {2560, 1440, 1, 32}, 4};
    assert(cache.observe(key, false).shouldLog);
    assert(!cache.observe(key, false).shouldLog);
    assert(cache.duplicateCount() == 1);
}

// Verifies that changed output fields form a new observation without a hash.
static void testChangedMetadataIsLogged() {
    DCCObservationCache cache;
    const DCCObservationKey first {DCCDiagnosticStage::AddrLibDccInfo, {2560, 1440, 256, 64}, 4};
    const DCCObservationKey changed {DCCDiagnosticStage::AddrLibDccInfo, {2560, 1440, 512, 64}, 4};
    assert(cache.observe(first, false).shouldLog);
    assert(cache.observe(changed, false).shouldLog);
}

// Verifies that every failure is logged even if its fields repeat.
static void testRepeatedFailureIsAlwaysLogged() {
    DCCObservationCache cache;
    const DCCObservationKey failure {DCCDiagnosticStage::FramebufferCapability, {1, 0xE00002C2}, 2};
    assert(cache.observe(failure, true).shouldLog);
    assert(cache.observe(failure, true).shouldLog);
    assert(cache.errorCount() == 2);
}
```

把四个新测试加入 `main()`。

- [ ] **步骤 2：运行测试并确认红灯**

运行前一任务的 clang++ 命令。预期：编译失败，提示缺少 `DCCDiagnosticsPolicy`、`DCCObservationCache` 或相关类型。

- [ ] **步骤 3：实现最小限频策略**

在 `DCCDiagnosticsPolicy.hpp` 增加以下公开接口：

```cpp
enum class DCCDiagnosticStage : uint32_t {
    AddrLibIdentity = 1,
    ScanoutDecision = 2,
    AddrLibDccInfo = 3,
    FramebufferCapability = 4,
};

struct DCCObservationKey {
    static constexpr size_t MaxFields = 16;
    DCCDiagnosticStage stage;
    uint32_t fields[MaxFields];
    size_t fieldCount;

    // Compares the complete observation without a hash or digest.
    bool equals(const DCCObservationKey &other) const;
};

struct DCCObservationDecision {
    bool shouldLog;
    uint32_t sequence;
};

class DCCObservationCache {
    public:
    static constexpr size_t Capacity = 32;

    // Logs new values and failures while suppressing exact successful duplicates.
    DCCObservationDecision observe(const DCCObservationKey &key, bool failure);

    // Returns the number of exact successful duplicates suppressed so far.
    uint32_t duplicateCount() const;

    // Returns the number of failure observations recorded so far.
    uint32_t errorCount() const;

    // Returns observations omitted after the fixed cache became full.
    uint32_t overflowCount() const;

    private:
    DCCObservationKey entries[Capacity] {};
    size_t entryCount {0};
    uint32_t sequenceValue {0};
    uint32_t duplicateValue {0};
    uint32_t errorValue {0};
    uint32_t overflowValue {0};
};

namespace DCCDiagnosticsPolicy {

// Restricts diagnostics to the approved Tahoe RX 6750 XT target and explicit diagnostic mode.
constexpr bool isTarget(bool isTahoe, uint32_t deviceId, uint32_t pciRevision, bool diagnosticsRequested) {
    return isTahoe && deviceId == 0x73DF && pciRevision == 0xC0 && diagnosticsRequested;
}

}    // namespace DCCDiagnosticsPolicy
```

实现要求：

- `equals()` 逐字段比较，不使用 hash；
- failure 每次增加 sequence 与 error，并返回 `shouldLog=true`；
- 新成功值加入数组并记录；
- 完全相同的成功值只增加 duplicate；
- 容量满后增加 overflow，第一次 overflow 返回 `shouldLog=true` 以记录容量状态，之后返回 false；
- 所有方法添加用途注释。

- [ ] **步骤 4：运行测试并确认绿灯**

运行 clang++ 测试。预期：退出码 0。

- [ ] **步骤 5：提交限频策略**

```sh
git add NootRX/DCCDiagnosticsPolicy.hpp Tests/DCCDiagnosticsPolicyTests.cpp
git commit -m "feat: add bounded DCC diagnostic policy"
```

### 任务 3：增加 kernel-only 诊断服务与 IORegistry 快照

**文件：**
- 创建：`NootRX/DCCDiagnostics.hpp`
- 创建：`NootRX/DCCDiagnostics.cpp`
- 修改：`NootRX/NootRX.hpp`
- 修改：`NootRX/NootRX.cpp`
- 修改：`NootRX.xcodeproj/project.pbxproj`
- 创建：`Tests/DCCDiagnosticsIntegrationTests.sh`

- [ ] **步骤 1：编写失败的集成守卫**

创建 `Tests/DCCDiagnosticsIntegrationTests.sh`。复用现有集成测试的 SDK 预处理方式，分别生成 `NootRX.cpp`、`X6000.cpp`、`X6000FB.cpp`、`DCCDiagnostics.cpp` 的有效源码。脚本位置参数固定为 `main_source`、`x6000_source`、`framebuffer_source`、`diagnostics_source`、`binary`，前四项提供仓库内默认路径，第五项为可选构建二进制，然后验证：

```sh
rg -F 'DCCDiagnosticsPolicy::isTarget(' "$main_effective" >/dev/null || fail "DCC diagnostic target gate is missing"
rg -F 'NootRX_DCCDiagEnabled' "$diag_effective" >/dev/null || fail "DCC diagnostic enabled property is missing"
rg -F 'NootRX_DCCDiagSequence' "$diag_effective" >/dev/null || fail "DCC diagnostic sequence property is missing"
rg -F 'NootRX_DCCDiagDuplicateCount' "$diag_effective" >/dev/null || fail "DCC duplicate counter is missing"
rg -F 'NootRX_DCCDiagErrorCount' "$diag_effective" >/dev/null || fail "DCC error counter is missing"
rg -F 'DCCDIAG' "$diag_effective" >/dev/null || fail "DCC diagnostic log marker is missing"
```

脚本必须预处理真实源码以排除注释和 `#if 0` 假阳性，并接受可选的构建二进制参数；提供二进制时还要用 `strings -a` 检查 `NootRX_DCCDiagEnabled` 和 `DCCDIAG`。

- [ ] **步骤 2：运行守卫并确认红灯**

```sh
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：FAIL，提示诊断 target gate 尚未接入。

- [ ] **步骤 3：声明职责单一的诊断服务**

创建 `NootRX/DCCDiagnostics.hpp`，公开接口固定为：

```cpp
class IOPCIDevice;

class DCCDiagnostics {
    public:
    // Configures the target gate and publishes whether DCC diagnostics are active.
    void configure(IOPCIDevice *provider, bool enabled);

    // Returns whether any DCC diagnostic route may be installed or emit data.
    bool isEnabled() const;

    // Records the exact engine/family/revision passed to AddrLib creation.
    void recordAddrLibIdentity(uint32_t returnCode, uint32_t chipEngine, uint32_t chipFamily,
        uint32_t chipRevision);

    // Records the unmodified inputs and result of shouldAllocScanoutDcc.
    void recordScanoutDecision(uint32_t width, uint32_t height, uint32_t candidateFlags,
        uint32_t pixelFormatSelector, bool result);

    // Records the unmodified AddrLib2 DCC request and result after size validation.
    void recordAddrLibDccInfo(uint32_t returnCode, const AppleAddr2ComputeDccInfoInputV1 *input,
        const AppleAddr2ComputeDccInfoOutputV1 *output);

    // Records request 0x1A before and after the original Framebuffer method.
    void recordFramebufferCapability(uint32_t returnCode, const AppleDccCapsParametersV1 *input,
        const AppleDccCapabilitiesV1 *output);
};
```

每个方法必须带用途注释；类的私有实现包含 provider、`IOLock`、`DCCObservationCache` 和一个只接收完整 `DCCObservationKey` 的内部发布函数。

- [ ] **步骤 4：实现限频日志与 IORegistry 发布**

`DCCDiagnostics.cpp` 的行为固定如下：

- `configure()` 在 `enabled=false` 时只发布 `NootRX_DCCDiagEnabled=0`；
- `enabled=true` 时分配锁并发布 `NootRX_DCCDiagEnabled=1`；
- 每个 record 方法先检查 enabled 和 ABI version/size；
- ABI 不匹配时以 failure 形式记录实际 version/size，不读取后续字段；
- 在锁内执行 cache 决策和计数更新，在锁外执行日志输出；
- 每次观察都更新三个计数属性；只有 `shouldLog=true` 时才发布完整 last snapshot；
- 不发布 GPU 地址、内核指针或 `pMipInfo`。

统一属性至少包含：

```text
NootRX_DCCDiagEnabled
NootRX_DCCDiagSequence
NootRX_DCCDiagDuplicateCount
NootRX_DCCDiagErrorCount
NootRX_DCCDiagOverflowCount
NootRX_DCCDiagLastStage
NootRX_DCCDiagLastReturnCode
NootRX_DCCDiagLastWidth
NootRX_DCCDiagLastHeight
NootRX_DCCDiagLastPixelFormat
NootRX_DCCDiagLastSwizzleMode
NootRX_DCCDiagLastBpp
NootRX_DCCDiagLastMetaPitch
NootRX_DCCDiagLastMetaHeight
NootRX_DCCDiagLastMetaSize
NootRX_DCCDiagLastMetaAlign
NootRX_DCCDiagLastCapabilityBits
```

日志格式使用一个稳定前缀：

```text
NootRX: DCCDIAG seq=<n> stage=<identity|scanout|addrlib|framebuffer> rc=0x........ ...
```

- [ ] **步骤 5：在主类中接入目标门控**

在 `NootRXMain::processPatcher()` 完成设备识别后调用：

```cpp
this->dccDiagnostics.configure(this->dGPU,
    DCCDiagnosticsPolicy::isTarget(getKernelVersion() == KernelVersion::Tahoe, this->deviceId,
        this->pciRevision, this->powerDiagnostics));
```

在 `NootRX.hpp` 增加带注释的访问器：

```cpp
// Returns the shared DCC diagnostic recorder used by accelerator and framebuffer routes.
DCCDiagnostics &getDCCDiagnostics() { return this->dccDiagnostics; }
```

该调用不得修改 `GPUDCCDisplayablePolicy`，也不得更改 PowerPlay profile。

- [ ] **步骤 6：把实现文件加入 Xcode Sources phase**

在 `NootRX.xcodeproj/project.pbxproj` 增加 `DCCDiagnostics.cpp` file reference、build file 与 Sources entry。完成后验证恰好出现一次 Sources entry：

```sh
rg -n 'DCCDiagnostics.cpp in Sources' NootRX.xcodeproj/project.pbxproj
```

预期：一行 PBXBuildFile 定义和一行 Sources phase 引用，不得出现第二个编译条目。

- [ ] **步骤 7：运行策略与集成测试**

运行：

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-diag-service.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDiagnosticsPolicyTests.cpp -o "$test_dir/dcc-diag-tests"
"$test_dir/dcc-diag-tests"
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：策略测试 PASS；集成守卫仍可因 route 尚未加入而失败，但 target gate、属性与日志检查必须通过。把 route 失败保留给任务 4/5 的红灯。

- [ ] **步骤 8：提交诊断服务**

```sh
git add NootRX/DCCDiagnostics.hpp NootRX/DCCDiagnostics.cpp NootRX/NootRX.hpp NootRX/NootRX.cpp NootRX.xcodeproj/project.pbxproj Tests/DCCDiagnosticsIntegrationTests.sh
git commit -m "feat: add DCC diagnostic recorder"
```

### 任务 4：路由 accelerator 的 AddrLib 与 scanout 决策

**文件：**
- 修改：`NootRX/X6000.hpp`
- 修改：`NootRX/X6000.cpp`
- 修改：`Tests/DCCDiagnosticsIntegrationTests.sh`

- [ ] **步骤 1：扩展集成守卫并确认红灯**

在脚本中检查以下三个完整 symbol 与 wrapper：

```text
__ZN33AMDRadeonX6000_AMDHWAlignManager24initEP30AMDRadeonX6000_IAMDHWInterface
__ZNK36AMDRadeonX6000_AMDAccelResourceAddr221shouldAllocScanoutDccEjjjj
__ZN33AMDRadeonX6000_AMDHWAlignManager211getDccInfo2EP28_ADDR2_COMPUTE_DCCINFO_INPUTP29_ADDR2_COMPUTE_DCCINFO_OUTPUT
```

还要检查 route 块受以下条件保护：

```cpp
NootRXMain::callback->getDCCDiagnostics().isEnabled()
```

运行脚本，预期因第一个缺失 route 失败。

- [ ] **步骤 2：声明原函数与 wrapper**

在 `X6000.hpp` 增加：

```cpp
mach_vm_address_t orgAlignManagerInit {0};
mach_vm_address_t orgShouldAllocScanoutDcc {0};
mach_vm_address_t orgGetDccInfo2 {0};

// Calls the original AddrLib initialiser and records the exact create identity getters.
static IOReturn wrapAlignManagerInit(void *that, void *hardwareInterface);

// Calls the original scanout decision and records its unmodified inputs and result.
static bool wrapShouldAllocScanoutDcc(void *that, UInt32 width, UInt32 height, UInt32 candidateFlags,
    UInt32 pixelFormatSelector);

// Calls the original AddrLib DCC computation and records its unmodified input and output.
static IOReturn wrapGetDccInfo2(void *that, const AppleAddr2ComputeDccInfoInputV1 *input,
    AppleAddr2ComputeDccInfoOutputV1 *output);
```

- [ ] **步骤 3：实现只读 wrapper**

`wrapAlignManagerInit` 必须先调用原函数，然后使用一个带用途注释的虚表读取帮助方法读取 `0x140/0x1B8/0x1A8` 三个 getter，并记录返回码与三个值。帮助方法只接受已确认的字节偏移：

```cpp
// Invokes one no-argument UInt32 getter from the Tahoe hardware-interface vtable.
static UInt32 callHardwareInterfaceGetter(void *hardwareInterface, size_t byteOffset) {
    using Getter = UInt32 (*)(void *);
    auto **vtable = *reinterpret_cast<void ***>(hardwareInterface);
    return reinterpret_cast<Getter>(vtable[byteOffset / sizeof(void *)])(hardwareInterface);
}
```

`wrapShouldAllocScanoutDcc` 和 `wrapGetDccInfo2` 必须遵循同一规则：

1. 保存原参数；
2. 调用原函数一次；
3. 将原参数、原输出和原返回值交给 recorder；
4. 原样返回；
5. 不写入 `that`、input、output 或返回值。

- [ ] **步骤 4：只在目标诊断模式路由三个函数**

在现有 `getHWInfo` route 和 Navi22 patch 保持不变的前提下，增加独立 route 数组。route 失败时使用 `PANIC_COND`，因为安装了部分 wrapper 会使证据链不完整；未启用诊断时完全不执行这些 route。

- [ ] **步骤 5：运行集成守卫**

```sh
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：三个 accelerator route 与只读调用顺序检查通过；Framebuffer route 仍为红灯。

- [ ] **步骤 6：提交 accelerator 诊断路由**

```sh
git add NootRX/X6000.hpp NootRX/X6000.cpp Tests/DCCDiagnosticsIntegrationTests.sh
git commit -m "feat: trace DCC accelerator decisions"
```

### 任务 5：路由 Framebuffer request 0x1A capability 往返

**文件：**
- 修改：`NootRX/X6000FB.hpp`
- 修改：`NootRX/X6000FB.cpp`
- 修改：`Tests/DCCDiagnosticsIntegrationTests.sh`

- [ ] **步骤 1：增加 Framebuffer route 红灯检查**

集成脚本检查完整 symbol：

```text
__ZN34AMDRadeonX6000_AmdRadeonController28callPlatformFunctionFromDrvrEjPvS0_S0_
```

并检查 wrapper 中同时存在：

```cpp
requestType == 0x1A
FunctionCast(wrapCallPlatformFunctionFromDrvr, callback->orgCallPlatformFunctionFromDrvr)
recordFramebufferCapability
```

运行脚本，预期因 route 尚未实现失败。

- [ ] **步骤 2：声明 Framebuffer 原函数与 wrapper**

在 `X6000FB.hpp` 增加：

```cpp
mach_vm_address_t orgCallPlatformFunctionFromDrvr {0};

// Records request 0x1A around the original Framebuffer handler without changing the request.
static IOReturn wrapCallPlatformFunctionFromDrvr(void *that, UInt32 requestType, void *param1,
    void *param2, void *param3);
```

- [ ] **步骤 3：实现 0x1A 专用只读记录**

wrapper 必须：

- 对非 `0x1A` 请求直接调用并返回原函数；
- 对 `0x1A` 请求，将 `param1` 视作 `AppleDccCapsParametersV1`，`param2` 视作 `AppleDccCapabilitiesV1`；
- 先调用原函数，再记录输入、最终输出和返回码；
- 不修改三个 param 指向的数据；
- 版本不为 1 时由 recorder 记录 ABI mismatch，不在 wrapper 内读取其他字段。

- [ ] **步骤 4：在诊断模式安装 route**

把 route 放在 `X6000FB::processKext()` 的 target diagnostic 分支中，与现有 DAL logger route 解耦；现有 logger 是否由 debug build 启用不能决定 0x1A route 是否安装。

- [ ] **步骤 5：运行完整集成守卫并确认绿灯**

```sh
sh Tests/DCCDiagnosticsIntegrationTests.sh
```

预期：PASS，确认四个 route、目标门控、属性和日志均存在。

- [ ] **步骤 6：提交 Framebuffer 诊断路由**

```sh
git add NootRX/X6000FB.hpp NootRX/X6000FB.cpp Tests/DCCDiagnosticsIntegrationTests.sh
git commit -m "feat: trace framebuffer DCC capabilities"
```

### 任务 6：构建、回归验证与诊断文档

**文件：**
- 修改：`README.md`
- 创建：`docs/validation/2026-09-21-rx6750xt-dcc-diagnostic-runbook.md`

- [ ] **步骤 1：编写实机运行手册**

手册必须固定两阶段顺序：

1. 诊断 kext + 当前 `GPUDCCDisplayable=No` 启动，确认没有行为变化；
2. 用户明确确认后，只改变 Displayable DCC 一个变量进行一次复现。

手册包含以下采集命令：

```sh
ioreg -lw0 -p IOService | rg 'NootRX_DCCDiag|GPUDCCDisplayable|recoveryCount'
log show --last boot --style compact --predicate 'eventMessage CONTAINS[c] "DCCDIAG" OR eventMessage CONTAINS[c] "GFX is hung" OR eventMessage CONTAINS[c] "GPU Reset" OR eventMessage CONTAINS[c] "watchdog"'
ls -lt /Library/Logs/DiagnosticReports/Kernel_*.gpuRestart /Library/Logs/DiagnosticReports/Retired/Kernel-*.panic 2>/dev/null
```

证据归类规则写死为：

- identity family/revision 与预期不符：进入 AddrLib ASIC 标识修复设计；
- Framebuffer capability 成功但 `getDccInfo2` 返回失败或输出为零：进入 AddrLib 输入/metadata 修复设计；
- AddrLib 输出稳定但 Framebuffer capability 对相同宽高/格式变化：进入 0x1A capability 转换修复设计；
- 只有某一像素格式、candidateFlags 或 swizzle 组合失败：进入精确 surface 筛选设计；
- 所有 displayable 组合均不可兼容：才评估自动关闭 Displayable DCC。

- [ ] **步骤 2：更新 README 的诊断边界**

README 说明：

- `-NRXPowerDiag` 在目标机器上会额外安装四个 DCC 只读 wrapper；
- 诊断版默认不改变 `GPUDCCDisplayable`；
- 日志限频、不记录地址；
- 当前 `nootrx-gpu-dcc-displayable=0` 仍是稳定回滚路径；
- Metal、OpenDesign 和 VideoToolbox 不被禁用。

- [ ] **步骤 3：运行所有主机测试**

```sh
test_dir=$(mktemp -d /private/tmp/nootrx-dcc-diag-regression.XXXXXX)
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDiagnosticsPolicyTests.cpp -o "$test_dir/dcc-diag-tests"
"$test_dir/dcc-diag-tests"
clang++ -std=c++17 -Wall -Wextra -Werror Tests/DCCDisplayablePolicyTests.cpp -o "$test_dir/dcc-displayable-tests"
"$test_dir/dcc-displayable-tests"
clang++ -std=c++17 -Wall -Wextra -Werror Tests/PowerProfileTests.cpp -o "$test_dir/power-profile-tests"
"$test_dir/power-profile-tests"
for test_file in Tests/*Tests.sh; do sh "$test_file"; done
```

预期：全部退出码为 0。

- [ ] **步骤 4：构建 Release x86_64 kext**

```sh
xcodebuild -project NootRX.xcodeproj -configuration Release -arch x86_64 clean build CODE_SIGNING_ALLOWED=NO
```

预期：`** BUILD SUCCEEDED **`。

- [ ] **步骤 5：验证构建产物**

```sh
plutil -lint build/Release/NootRX.kext/Contents/Info.plist
sh Tests/DCCDiagnosticsIntegrationTests.sh NootRX/NootRX.cpp NootRX/X6000.cpp NootRX/X6000FB.cpp NootRX/DCCDiagnostics.cpp build/Release/NootRX.kext/Contents/MacOS/NootRX
strings -a build/Release/NootRX.kext/Contents/MacOS/NootRX | rg 'DCCDIAG|NootRX_DCCDiagEnabled|shouldAllocScanoutDcc|getDccInfo2|callPlatformFunctionFromDrvr'
```

预期：plist 合法、集成守卫 PASS、五组关键字符串存在。

- [ ] **步骤 6：检查变更范围**

```sh
git diff --check
git status --short
git diff --stat master...HEAD
```

预期：不包含 `NootRX/Firmware/sdma_5_2_4_ucode.bin`、用户的问题记录或未命名 validation 文档。

- [ ] **步骤 7：提交文档与最终验证记录**

```sh
git add README.md docs/validation/2026-09-21-rx6750xt-dcc-diagnostic-runbook.md
git commit -m "docs: add DCC diagnostic runbook"
```

### 任务 7：安装诊断候选版并验证稳定基线

**执行关卡：** 只有用户确认已挂载目标 EFI 后才能执行；不得猜测卷名或覆盖未核对的 EFI。

- [ ] **步骤 1：只读核对目标 EFI**

核对实际挂载路径、`config.plist` 中 NootRX 条目、当前 kext bundle version、boot-args 中仍有 `nootrx-gpu-dcc-displayable=0`。如果任何一项不一致，停止并向用户报告。

- [ ] **步骤 2：备份并替换 NootRX.kext**

使用包含日期与用途的备份目录名，例如：

```text
NootRX.kext.backup-20260921-before-dcc-diagnostics
```

只替换 `EFI/OC/Kexts/NootRX.kext`，不修改 config。完成后运行 `plutil -lint` 并用 `cmp` 比较 build 与 EFI 中的主二进制，预期退出码 0。

- [ ] **步骤 3：用户重启后验证 DCC 关闭基线**

确认：

```text
NootRX_DCCDiagEnabled = 1
GPUDCCDisplayable = No
NootRX_GPUDCCDisplayable = 0
recoveryCount = 0
```

并检查启动日志中 identity stage 只出现一次，没有 ABI mismatch、route failure、GPU reset 或 watchdog。

- [ ] **步骤 4：在基线验证结果上设置检查点**

如果基线异常，立即恢复 kext 备份并停止。基线正常时，把 IORegistry 和日志保存到 validation 文档，然后等待用户批准 DCC 开启复现。

### 任务 8：执行一次 DCC 开启复现并形成根因结论

**执行关卡：** 只有用户明确批准改变 DCC 变量后执行。其他 PowerPlay、Chrome 和显示配置保持不变。

- [ ] **步骤 1：备份 config 并只改变 DCC 变量**

先验证 boot-args 中恰好存在一次 `nootrx-gpu-dcc-displayable=0`，再将其删除或替换为显式 `=1`；两种方式的选择在执行前由用户确认。不得同时更改 ULV、GFXOFF、Falcon、WorkLoadPolicy 或 Chrome 参数。

- [ ] **步骤 2：重启后确认 DCC 确实开启**

确认 `GPUDCCDisplayable=Yes`、诊断 enabled、PowerPlay 四项仍与基线一致，然后才打开相同 Chrome 页面。

- [ ] **步骤 3：复现并立即采集**

出现闪烁、画面冻结、GFX hang、GPU reset 或 watchdog 任一现象后，采集 runbook 中的 IORegistry、统一日志与诊断报告。如果系统自动重启，下一次启动后读取上一启动日志和报告。

- [ ] **步骤 4：恢复稳定基线**

把 boot-args 恢复为 `nootrx-gpu-dcc-displayable=0` 并重启；不让诊断实验配置继续长期运行。

- [ ] **步骤 5：按单一断裂点形成结论**

将 identity、scanout、Framebuffer capability、AddrLib metadata 四类记录按 sequence 和宽高/格式组合对齐，只报告第一个发生不一致的边界。根据任务 6 的归类规则选择下一份“单点修复设计”；在获得用户批准前不修改任何 DCC 值。

## 完成标准

- 静态 ABI 证据、结构大小和字段偏移可复查；
- 诊断关闭时不安装任何新 route；
- 诊断开启时四个 wrapper 均只读且限频；
- 当前 DCC 关闭基线无行为变化；
- 一次 DCC 开启复现后，能把问题定位到 AddrLib identity、metadata、0x1A capability 或特定 surface 之一；
- 所有代码与文档已提交并推送，但真正的修复补丁必须依据采集证据另行设计。
