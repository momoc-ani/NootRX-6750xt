# NootRX ![GitHub Workflow Status](https://img.shields.io/github/actions/workflow/status/ChefKissInc/NootRX/main.yml?branch=master&logo=github&style=for-the-badge)

The unsupported AMD rDNA 2 dedicated GPU kext.

The NootRX project is Copyright © 2023-2024 ChefKiss. The NootRX project is licensed under the `Thou Shalt Not Profit License version 1.5`. See `LICENSE`.

> [!NOTE]
> This project is under active research and development; There will be some minor issues here and there, but is almost perfectly functional.
>
> See repository issues and [our site](https://chefkiss.dev/applehax/nootrx) for more information.

Thanks [Acidanthera](https://github.com/Acidanthera) for the UnfairGVA patches in [WhateverGreen](https://github.com/Acidanthera/WhateverGreen).

## RX 6750 XT stability fork

This fork contains a targeted stability workaround for the AMD Radeon RX 6750 XT
(`PCI device 0x73DF`, revision `0xC0`) on macOS.

### What was changed

During a long-running failure, the system reported an `AMDRadeonX6000` GFX
channel hang. GPU reset attempts failed and WindowServer eventually triggered a
userspace watchdog panic. The failure pattern is consistent with a GPU power
state transition problem reported by other RX 6750 XT users.

When NootRX injects the framebuffer personality, this fork changes the following
PowerPlay properties only for `0x73DF/0xC0`:

```text
PP_DisableULV = 1
PP_Falcon_QuickTransition_Enable = 0
PP_GfxOffControl = 0
PP_WorkLoadPolicyMask = 0
```

The workaround keeps the normal Navi22 device detection, firmware, DDI caps,
Metal, VideoToolbox, and hardware acceleration paths. It does not spoof the
card as an RX 6800/6900 and does not disable GPU acceleration. Other supported
GPU IDs retain the upstream behavior.

The tradeoff is higher idle power and potentially slightly higher idle
temperature because the deepest GPU power transitions are restricted.

Tahoe uses the same Navi22 GC compatibility selector as the other supported
macOS releases: both `_gc_sw_init` and `_gc_set_fw_entry_info` report GC 10.3.4.
This keeps queue, KIQ, RLC setup, and firmware descriptor selection aligned
with Tahoe's available GC Hub implementation while retaining the native Navi22
GC 10.3.2 command processor and RLC firmware. Metal and OpenDesign
acceleration remain enabled.

Tahoe also keeps the native Navi23 donor DDI capability table when NootRX maps
it to Navi22. In particular, the Tahoe-provided feature word `0x42000020` is no
longer overwritten by the older universal `0x42040028` value. Other macOS and
GPU paths retain the upstream universal table. This isolated change does not
alter the PowerPlay workaround, GC selector, Metal, OpenDesign, or VideoToolbox.

The effective word from both Apple driver tables is published to IORegistry as
`NootRX_DDICaps_X6000FB` and `NootRX_DDICaps_HWLibs`. On Tahoe with Navi22,
both properties should report decimal `1107296288` (`0x42000020`).

### Build

Initialize the submodules and build the release kext with:

```sh
git submodule update --init
xcodebuild -configuration Release -arch x86_64 build
```

The resulting kext is located at `build/Release/NootRX.kext`. Back up the kext
currently used by OpenCore before replacing it. Keep Lilu before NootRX in the
OpenCore kext order.

After reboot, verify the active properties with `ioreg`:

```text
PP_DisableULV = 1
PP_Falcon_QuickTransition_Enable = 0
PP_GfxOffControl = 0
PP_WorkLoadPolicyMask = 0
```

### Independent experiment boot-args

The stable values above remain the default. Each argument below is optional and
overrides exactly one property for the RX 6750 XT. Values are the actual driver
values, so `nootrx-pp-disable-ulv=0` restores ULV while the other three stay at
their stable values:

```text
nootrx-pp-disable-ulv=0
nootrx-pp-gfxoff-control=1
nootrx-pp-falcon-quick-transition=1
nootrx-pp-workload-policy-mask=16
```

Use `-NRXPowerDiag` only during an approved experiment. It enables the existing
AMDRadeonX6000 PowerPlay/DAL logger routes and records the effective profile in
the NootRX log before a GPU panic is raised. It is intentionally separate from
`-NRXDebug` and is disabled by default.

The effective profile is also mirrored to IORegistry as
`NootRXPowerProfileMask`, `NootRX_PP_DisableULV`,
`NootRX_PP_GfxOffControl`, `NootRX_PP_Falcon_QuickTransition_Enable`, and
`NootRX_PP_WorkLoadPolicyMask`.

### Ordered validation plan

The detailed, interactive plan is in
`docs/validation/2026-08-28-rx6750xt-power-matrix.md`. It uses a
four-bit Gray-code order so each reboot changes one property. Start from the
stable profile (`mask=0`), run the specified display-off/wake and Metal workload
checks, and continue only after the observation window is clean. Any new
`Kernel_*.gpuRestart`, `GPU Reset failed`, `GFX is hung`, or WindowServer
watchdog event stops the sequence and requires reverting to `mask=0`.

Useful queries after a reboot or reset:

```sh
log show --last 24h --style compact --predicate 'eventMessage CONTAINS[c] "NootRX" OR eventMessage CONTAINS[c] "GPU Reset" OR eventMessage CONTAINS[c] "GFX is hung" OR eventMessage CONTAINS[c] "watchdog"'
ls -lt /Library/Logs/DiagnosticReports/Kernel_*.gpuRestart
ls -lt /Library/Logs/DiagnosticReports/Retired/Kernel-*.panic
ioreg -l -w0 -p IOService | grep -E 'NootRXPowerProfileMask|NootRX_PP_'
ioreg -l -w0 -r -c IOAccelerator | grep -E 'recoveryCount|Temperature|MetalPluginName'
```

### Chromium GPU rasterization flicker workaround

On macOS Tahoe with the RX 6750 XT, Chromium-based applications may briefly
show cyan, green, red, or white rectangles over small text regions. The issue
can be visible on the physical display while being absent from a macOS
screenshot. Safari does not reproduce the same symptom.

The current evidence localises the failure to Chromium's GPU rasterization
path rather than the page itself or the complete hardware acceleration stack:

```text
Default Chrome                              -> flickers
Chrome with zero-copy disabled              -> flickers
Chrome with all GPU acceleration disabled   -> does not flicker
Chrome with only GPU rasterization disabled -> does not flicker
Safari                                      -> does not flicker
```

No concurrent `GFX is hung`, `GPU Reset failed`, Metal context-loss, or
WindowServer watchdog event was recorded during the captured flicker. This
means the proven failure boundary is Chromium/Skia GPU rasterization interacting
with the active AMD Metal driver. The exact low-level driver defect is not yet
proven, and the workaround must not be presented as a fix for GPU reset or
system-freeze events.

For Google Chrome, open the following page and set **GPU rasterization** to
**Disabled**, then restart Chrome:

```text
chrome://flags/#enable-gpu-rasterization
```

The equivalent command-line switch is:

```sh
open -na "Google Chrome" --args --disable-gpu-rasterization
```

This keeps GPU compositing, Metal/WebGL, and video hardware decoding enabled;
only webpage rasterization falls back to the CPU. To revert, restore the flag
to **Default** and restart Chrome.
