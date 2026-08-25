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
