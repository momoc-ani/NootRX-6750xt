// Copyright © 2023-2024 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#include "X6000.hpp"
#include "DCCRouteValidation.hpp"
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

        auto &diagnostics = NootRXMain::callback->getDCCDiagnostics();
        if (diagnostics.isEnabled()) {
            mach_vm_address_t alignManagerInitAddress = 0;
            mach_vm_address_t shouldAllocScanoutDccAddress = 0;
            mach_vm_address_t getDccInfo2Address = 0;
            KernelPatcher::SolveRequest diagnosticSymbols[] = {
                {"__ZN33AMDRadeonX6000_AMDHWAlignManager24initEP30AMDRadeonX6000_IAMDHWInterface",
                    alignManagerInitAddress},
                {"__ZNK36AMDRadeonX6000_AMDAccelResourceAddr221shouldAllocScanoutDccEjjjj",
                    shouldAllocScanoutDccAddress},
                {"__ZN33AMDRadeonX6000_AMDHWAlignManager211getDccInfo2EP28_ADDR2_COMPUTE_DCCINFO_INPUTP29_ADDR2_COMPUTE_DCCINFO_OUTPUT",
                    getDccInfo2Address},
            };

            if (!patcher.solveMultiple(id, diagnosticSymbols, slide, size)) {
                diagnostics.disable(DCCDiagnosticFailureCode::AcceleratorSymbol,
                    "failed to resolve accelerator diagnostic symbols");
            } else {
                // Keep each validator inside the already range-checked kext image.
                const auto alignManagerAvailable = size - static_cast<size_t>(alignManagerInitAddress - slide);
                const auto scanoutAvailable = size - static_cast<size_t>(shouldAllocScanoutDccAddress - slide);
                const auto getDccInfoAvailable = size - static_cast<size_t>(getDccInfo2Address - slide);
                const auto signaturesValid = DCCRouteValidation::validateAlignManagerInit(
                                                 reinterpret_cast<const uint8_t *>(alignManagerInitAddress),
                                                 alignManagerAvailable) &&
                                             DCCRouteValidation::validateShouldAllocScanoutDcc(
                                                 reinterpret_cast<const uint8_t *>(shouldAllocScanoutDccAddress),
                                                 scanoutAvailable) &&
                                             DCCRouteValidation::validateGetDccInfo2(
                                                 reinterpret_cast<const uint8_t *>(getDccInfo2Address),
                                                 getDccInfoAvailable);
                if (!signaturesValid) {
                    diagnostics.disable(DCCDiagnosticFailureCode::AcceleratorSignature,
                        "accelerator diagnostic signature mismatch");
                } else {
                    KernelPatcher::RouteRequest diagnosticRoutes[] = {
                        {"__ZN33AMDRadeonX6000_AMDHWAlignManager24initEP30AMDRadeonX6000_IAMDHWInterface",
                            wrapAlignManagerInit, this->orgAlignManagerInit},
                        {"__ZNK36AMDRadeonX6000_AMDAccelResourceAddr221shouldAllocScanoutDccEjjjj",
                            wrapShouldAllocScanoutDcc, this->orgShouldAllocScanoutDcc},
                        {"__ZN33AMDRadeonX6000_AMDHWAlignManager211getDccInfo2EP28_ADDR2_COMPUTE_DCCINFO_INPUTP29_ADDR2_COMPUTE_DCCINFO_OUTPUT",
                            wrapGetDccInfo2, this->orgGetDccInfo2},
                    };
                    if (!patcher.routeMultiple(id, diagnosticRoutes, slide, size)) {
                        diagnostics.disable(DCCDiagnosticFailureCode::AcceleratorRoute,
                            "failed to route accelerator diagnostics");
                    } else {
                        diagnostics.markRouteReady(DCCDiagnostics::AcceleratorRoute);
                    }
                }
            }
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
    const auto ret = callback->orgAlignManagerInit(that, hardwareInterface);

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
    const auto ret = callback->orgShouldAllocScanoutDcc(that, width, height, candidateFlags, pixelFormatSelector);
    NootRXMain::callback->getDCCDiagnostics().recordScanoutDecision(
        width, height, candidateFlags, pixelFormatSelector, ret);
    return ret;
}

IOReturn X6000::wrapGetDccInfo2(void *that, const AppleAddr2ComputeDccInfoInputV1 *input,
    AppleAddr2ComputeDccInfoOutputV1 *output) {
    const auto ret = callback->orgGetDccInfo2(that, input, output);
    NootRXMain::callback->getDCCDiagnostics().recordAddrLibDccInfo(static_cast<UInt32>(ret), input, output);
    return ret;
}
