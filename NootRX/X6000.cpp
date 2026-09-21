// Copyright © 2023-2024 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#include "X6000.hpp"
#include "NootRX.hpp"
#include "PatcherPlus.hpp"
#include <Headers/kern_api.hpp>
#include <IOKit/IOService.h>

static const char *pathRadeonX6000 = "/System/Library/Extensions/AMDRadeonX6000.kext/Contents/MacOS/AMDRadeonX6000";

static KernelPatcher::KextInfo kextRadeonX6000 {
    "com.apple.kext.AMDRadeonX6000",
    &pathRadeonX6000,
    1,
    {},
    {},
    KernelPatcher::KextInfo::Unloaded,
};

X6000 *X6000::callback = nullptr;

void X6000::init() {
    SYSLOG("X6000", "Module initialised");

    callback = this;

    lilu.onKextLoadForce(&kextRadeonX6000);
}

bool X6000::processKext(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size) {
    if (kextRadeonX6000.loadIndex == id) {
        NootRXMain::callback->ensureRMMIO();

        RouteRequestPlus request {"__ZN35AMDRadeonX6000_AMDAccelVideoContext9getHWInfoEP13sHardwareInfo", wrapGetHWInfo,
            this->orgGetHWInfo};
        PANIC_COND(!request.route(patcher, id, slide, size), "X6000", "Failed to route getHWInfo");

        if (NootRXMain::callback->getDCCDiagnostics().isEnabled()) {
            RouteRequestPlus diagnosticRequests[] = {
                {"__ZN33AMDRadeonX6000_AMDHWAlignManager24initEP30AMDRadeonX6000_IAMDHWInterface",
                    wrapAlignManagerInit, this->orgAlignManagerInit},
                {"__ZNK36AMDRadeonX6000_AMDAccelResourceAddr221shouldAllocScanoutDccEjjjj",
                    wrapShouldAllocScanoutDcc, this->orgShouldAllocScanoutDcc},
                {"__ZN33AMDRadeonX6000_AMDHWAlignManager211getDccInfo2EP28_ADDR2_COMPUTE_DCCINFO_INPUTP29_ADDR2_COMPUTE_DCCINFO_OUTPUT",
                    wrapGetDccInfo2, this->orgGetDccInfo2},
            };
            PANIC_COND(!RouteRequestPlus::routeAll(patcher, id, diagnosticRequests, slide, size), "X6000",
                "Failed to route DCC diagnostic symbols");
        }

        if (NootRXMain::callback->attributes.isNavi22() && NootRXMain::callback->attributes.isVenturaAndLater()) {
            const LookupPatchPlus patch = {&kextRadeonX6000, kHwlConvertChipFamilyOriginal,
                kHwlConvertChipFamilyOriginalMask, kHwlConvertChipFamilyPatched, 1};
            PANIC_COND(!patch.apply(patcher, slide, size), "X6000",
                "Failed to apply Navi 22 HwlConvertChipFamily patch");
        }

        return true;
    }

    return false;
}

IOReturn X6000::wrapGetHWInfo(IOService *accelVideoCtx, void *hwInfo) {
    auto ret = FunctionCast(wrapGetHWInfo, callback->orgGetHWInfo)(accelVideoCtx, hwInfo);
    getMember<UInt16>(hwInfo, 0x4) = NootRXMain::callback->attributes.isNavi21() ? 0x73BF : 0x73FF;
    return ret;
}

// Invokes one no-argument UInt32 getter from the Tahoe hardware-interface vtable.
static UInt32 callHardwareInterfaceGetter(void *hardwareInterface, size_t byteOffset) {
    using Getter = UInt32 (*)(void *);
    auto **vtable = *reinterpret_cast<void ***>(hardwareInterface);
    return reinterpret_cast<Getter>(vtable[byteOffset / sizeof(void *)])(hardwareInterface);
}

IOReturn X6000::wrapAlignManagerInit(void *that, void *hardwareInterface) {
    const auto ret = FunctionCast(wrapAlignManagerInit, callback->orgAlignManagerInit)(that, hardwareInterface);

    UInt32 chipEngine = 0;
    UInt32 chipFamily = 0;
    UInt32 chipRevision = 0;
    if (hardwareInterface != nullptr) {
        chipEngine = callHardwareInterfaceGetter(hardwareInterface, 0x140);
        chipFamily = callHardwareInterfaceGetter(hardwareInterface, 0x1B8);
        chipRevision = callHardwareInterfaceGetter(hardwareInterface, 0x1A8);
    }
    NootRXMain::callback->getDCCDiagnostics().recordAddrLibIdentity(
        static_cast<UInt32>(ret), chipEngine, chipFamily, chipRevision);
    return ret;
}

bool X6000::wrapShouldAllocScanoutDcc(void *that, UInt32 width, UInt32 height, UInt32 candidateFlags,
    UInt32 pixelFormatSelector) {
    const auto ret = FunctionCast(wrapShouldAllocScanoutDcc, callback->orgShouldAllocScanoutDcc)(
        that, width, height, candidateFlags, pixelFormatSelector);
    NootRXMain::callback->getDCCDiagnostics().recordScanoutDecision(
        width, height, candidateFlags, pixelFormatSelector, ret);
    return ret;
}

IOReturn X6000::wrapGetDccInfo2(void *that, const AppleAddr2ComputeDccInfoInputV1 *input,
    AppleAddr2ComputeDccInfoOutputV1 *output) {
    const auto ret = FunctionCast(wrapGetDccInfo2, callback->orgGetDccInfo2)(that, input, output);
    NootRXMain::callback->getDCCDiagnostics().recordAddrLibDccInfo(static_cast<UInt32>(ret), input, output);
    return ret;
}
