# Tahoe Navi22 GC 运行时拆分实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 `superpowers:executing-plans` 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 修复 Tahoe + RX 6750 XT/Navi22 的 GC 运行时版本错配：让 `_gc_sw_init` 保留硬件报告的 GC 10.3.2，同时只对 `_gc_set_fw_entry_info` 保留 GC 10.3.4 描述符兼容映射，并通过 IORegistry 和启动日志发布实际选择。

**架构：** `HWLibs::processKext` 在 Tahoe + Navi22 分支跳过 `kGcSwInit*` 运行时补丁，调用 `NootRXMain::publishGCVersionSelection(0x0A0302, 0x0A0304)` 发布运行时与描述符的拆分选择；非 Tahoe 仍使用现有 `_gc_sw_init` 兼容补丁。现有两套 `_gc_set_fw_entry_info` 补丁、Navi22 原生 GC 10.3.2 固件和其他 DCC/电源/DDI 路径保持不变。

**技术栈：** C++17、OpenCore/Lilu 风格内核补丁、POSIX shell 守卫、Xcode Release x86_64 构建。

---

## 文件职责

- 修改：`Tests/GCVersionPolicyTests.sh`——验证 Tahoe 运行时/描述符拆分、非 Tahoe 兼容补丁、IORegistry 属性、日志和原生固件路径。
- 修改：`NootRX/HWLibs.cpp`——实现 Tahoe + Navi22 的运行时分支，并保留两套描述符补丁。
- 修改：`NootRX/HWLibs.hpp`——说明 `kGcSwInit*` 仅用于非 Tahoe 兼容路径。
- 修改：`NootRX/NootRX.hpp`——声明带用途注释的 GC 版本发布方法。
- 修改：`NootRX/NootRX.cpp`——实现 IORegistry 发布和启动日志。
- 修改：`README.md`——记录根因、拆分行为、IORegistry 观测值和验证边界。

### 任务 1：先写 GC 策略失败测试

**文件：** `Tests/GCVersionPolicyTests.sh`

- [x] **步骤 1：把测试契约改为已批准的拆分行为。**

  测试必须拒绝当前“所有系统统一 10.3.4”的实现，并检查以下可观察契约：

  ```sh
  if ! rg -U -q 'if \(getKernelVersion\(\) == KernelVersion::Tahoe\).*publishGCVersionSelection\(0x0A0302, 0x0A0304\)' "$hwlibs_source"; then
      printf '%s\n' "FAIL: Tahoe Navi22 must publish GC 10.3.2 runtime and 10.3.4 descriptor versions"
      exit 1
  fi
  if ! rg -U -q 'else \{.*kGcSwInitOriginal.*Failed to apply Navi 22 gc_sw_init patch' "$hwlibs_source"; then
      printf '%s\n' "FAIL: non-Tahoe Navi22 must retain the gc_sw_init compatibility patch"
      exit 1
  fi
  rg -n 'NootRX_GCRuntimeVersion|NootRX_GCDescriptorVersion' "$nootrx_source" >/dev/null
  rg -n 'Tahoe Navi22 GC runtime split' "$hwlibs_source" "$nootrx_source" >/dev/null
  ```

  两套 `_gc_set_fw_entry_info` 版本分支、`gc_10_3_2_` 固件前缀和构建产物检查继续保留，并把成功输出更新为拆分策略。

- [x] **步骤 2：运行测试确认当前生产代码正确失败。**

  运行：

  ```sh
  sh Tests/GCVersionPolicyTests.sh NootRX/HWLibs.cpp NootRX/HWLibs.hpp NootRX/NootRX.cpp
  ```

  预期：FAIL，原因是当前 `HWLibs.cpp` 无 Tahoe 分支且没有两个 GC 版本 IORegistry 属性。若测试因脚本语法或路径错误失败，先修正测试，不修改生产代码。

- [x] **步骤 3：提交测试红灯。**

  ```sh
  git add Tests/GCVersionPolicyTests.sh
  git commit -m "test: require Tahoe Navi22 GC runtime split"
  ```

### 任务 2：实现 Tahoe + Navi22 运行时/描述符拆分

**文件：** `NootRX/HWLibs.cpp`、`NootRX/HWLibs.hpp`、`NootRX/NootRX.hpp`、`NootRX/NootRX.cpp`

- [x] **步骤 1：修改 `HWLibs::processKext` 的 Navi22 分支。**

  将现有无条件 `kGcSwInit*` 应用改为：

  ```cpp
  if (getKernelVersion() == KernelVersion::Tahoe) {
      NootRXMain::callback->publishGCVersionSelection(0x0A0302, 0x0A0304);
      SYSLOG("HWLibs", "Tahoe Navi22 GC runtime split: runtime=0x%06X descriptor=0x%06X",
          0x0A0302, 0x0A0304);
  } else {
      const LookupPatchPlus patch {&kextRadeonX6810HWLibs, kGcSwInitOriginal, kGcSwInitOriginalMask,
          kGcSwInitPatched, kGcSwInitPatchedMask, 1};
      PANIC_COND(!patch.apply(patcher, slide, size), "HWLibs", "Failed to apply Navi 22 gc_sw_init patch");
  }
  ```

  保持随后 `isSonoma1404AndLater()` 与旧系统分支中的两套 `_gc_set_fw_entry_info` 补丁完全不变；不修改 PSP、SDMA、DCC、电源、DDI 或固件文件选择。

- [x] **步骤 2：增加 GC 选择发布接口。**

  在 `NootRX.hpp` 声明并注释：

  ```cpp
  // Publishes the selected GC runtime and firmware descriptor versions for post-boot diagnostics.
  void publishGCVersionSelection(UInt32 runtimeVersion, UInt32 descriptorVersion);
  ```

  在 `NootRX.cpp` 实现：

  ```cpp
  // Publishes the selected GC versions to IORegistry and records the split policy in the system log.
  void NootRXMain::publishGCVersionSelection(UInt32 runtimeVersion, UInt32 descriptorVersion) {
      this->dGPU->setProperty("NootRX_GCRuntimeVersion", runtimeVersion, 32);
      this->dGPU->setProperty("NootRX_GCDescriptorVersion", descriptorVersion, 32);
      SYSLOG("NootRX", "Tahoe Navi22 GC runtime split published: runtime=0x%06X descriptor=0x%06X",
          runtimeVersion, descriptorVersion);
  }
  ```

- [x] **步骤 3：更新 `HWLibs.hpp` 注释。**

  明确 `kGcSwInit*` 将硬件版本映射到 10.3.4，仅用于非 Tahoe 兼容路径；Tahoe 使用硬件报告的 10.3.2 运行时，描述符兼容仍由 `_gc_set_fw_entry_info` 补丁负责。

- [x] **步骤 4：运行策略测试确认绿灯。**

  运行：

  ```sh
  sh Tests/GCVersionPolicyTests.sh NootRX/HWLibs.cpp NootRX/HWLibs.hpp NootRX/NootRX.cpp
  ```

  预期：输出 `PASS: Tahoe Navi22 keeps GC 10.3.2 runtime and GC 10.3.4 descriptor compatibility`。

- [x] **步骤 5：提交生产实现。**

  ```sh
  git add NootRX/HWLibs.cpp NootRX/HWLibs.hpp NootRX/NootRX.cpp NootRX/NootRX.hpp
  git commit -m "fix: split Tahoe Navi22 GC runtime and descriptor versions"
  ```

### 任务 3：更新用户可读文档

**文件：** `README.md`

- [x] **步骤 1：替换旧的“统一 10.3.4”说明。**

  说明 14:50 GPU reset 的第一现场是 Metal ComputeUQ0，报告中的 `dcc_en=0` 排除了 Displayable DCC；当前修复针对 GC 10.3.2 固件与错误的 10.3.4 `_gc_sw_init` 路径错配。明确 Metal、OpenDesign、VideoToolbox 保持启用，非 Tahoe 行为不变。

- [x] **步骤 2：记录观测方式和边界。**

  给出 `ioreg -l -p IOService -n <GPU节点>` 中应观察到的：

  - `NootRX_GCRuntimeVersion = 0x0A0302`
  - `NootRX_GCDescriptorVersion = 0x0A0304`

  同时声明构建通过不等于长期死机已根治，实机需继续观察 GPU reset、GPU Reset failed 和 WindowServer watchdog 报告。

- [x] **步骤 3：提交文档。**

  ```sh
  git add README.md
  git commit -m "docs: describe Tahoe Navi22 GC runtime split"
  ```

### 任务 4：完整测试与 Release 构建

**文件：** 构建产物 `build/Release/NootRX.kext`

- [x] **步骤 1：运行所有 C++ 测试。**

  ```sh
  for source in Tests/*Tests.cpp; do
    clang++ -std=c++17 -Wall -Wextra -Werror "$source" -o "/tmp/$(basename "$source" .cpp)"
    "/tmp/$(basename "$source" .cpp)"
  done
  ```

- [x] **步骤 2：运行所有 shell 守卫。**

  ```sh
  for script in Tests/*Tests.sh; do
    sh "$script"
  done
  ```

- [x] **步骤 3：构建 Release x86_64 kext。**

  ```sh
  xcodebuild -project NootRX.xcodeproj -configuration Release -arch x86_64 clean build CODE_SIGNING_ALLOWED=NO
  ```

- [x] **步骤 4：用构建产物运行 GC 守卫。**

  ```sh
  sh Tests/GCVersionPolicyTests.sh \
    NootRX/HWLibs.cpp NootRX/HWLibs.hpp NootRX/NootRX.cpp \
    build/Release/NootRX.kext/Contents/MacOS/NootRX
  ```

  预期：源码和二进制均包含拆分日志，二进制仍包含 `gc_10_3_2_rlc_ucode.bin`。

- [ ] **步骤 5：提交构建验证记录。**

  ```sh
  git status --short
  git log -3 --oneline
  ```

  若工作区仅有计划、测试、实现和 README 的已提交变更，则继续推送；不要提交构建目录或临时测试二进制。

### 任务 5：推送与用户挂载 EFI 后部署

- [ ] **步骤 1：推送实现分支。**

  ```sh
  git push origin dcc-root-cause-diagnostics
  ```

- [ ] **步骤 2：用户挂载 EFI 后确认目标路径和当前 kext 备份。**

  只在用户明确表示 EFI 已挂载后读取挂载点，先检查目标 `EFI/OC/Kexts/NootRX.kext` 的修改时间与大小，再备份为同目录的 `.before-gc-runtime-split` 副本；不修改 `config.plist` 或 boot-args。

- [ ] **步骤 3：复制 Release kext 并核对。**

  复制 `build/Release/NootRX.kext` 到目标 Kexts 目录，随后用 `codesign --verify`（若目标环境允许）和 `find`/`stat` 核对文件结构；不得删除用户其他 kext。

- [ ] **步骤 4：重启后读取 IORegistry。**

  记录两个属性和启动日志，确认 runtime `0x0A0302`、descriptor `0x0A0304`；若再次死机，优先收集首份 `.gpuRestart` 的 pending channel、`dcc_en`、RSMU 初始状态和 reset 恢复结果。

## 完成标准

- GC 策略测试先在旧代码上红灯，再在新代码上绿灯。
- 所有 C++/shell 测试通过，Release x86_64 构建成功。
- 代码已提交并推送到 `origin/dcc-root-cause-diagnostics`。
- 用户挂载 EFI 后才部署 kext；不修改 OpenCore 参数和其他稳定性 workaround。
- 最终报告只声明补丁、测试、构建、推送和部署事实，不宣称长期 GPU 稳定性已经被代码测试证明。
