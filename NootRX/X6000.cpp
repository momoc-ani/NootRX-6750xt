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

        if (NootRXMain::callback->attributes.isNavi22() && NootRXMain::callback->attributes.isVenturaAndLater()) {
            const LookupPatchPlus patch = {&kextRadeonX6000, kHwlConvertChipFamilyOriginal,
                kHwlConvertChipFamilyOriginalMask, kHwlConvertChipFamilyPatched, 1};
            PANIC_COND(!patch.apply(patcher, slide, size), "X6000",
                "Failed to apply Navi 22 HwlConvertChipFamily patch");
        }

        auto *gfxDiag = NootRXMain::callback->getGfxDiag();
        if (gfxDiag->isEnabled()) {
            // Reset/hang probes: request a ring dump the moment the driver notices a hang or resets the GPU.
            RouteRequestPlus requests[] = {
                {"__ZN26AMDRadeonX6000_AMDHardware17dumpASICHangStateEb", wrapDumpAsicHangState,
                    this->orgResetProbes[0]},
                {"__ZN26AMDRadeonX6000_AMDHardware13resetHardwareEjPj", wrapResetHardware,
                    this->orgResetProbes[1]},
                {"__ZN29AMDRadeonX6000_AMDSWScheduler13signalTimeoutEmj", wrapSignalTimeout,
                    this->orgResetProbes[2]},
            };

            UInt32 probeMask = 0;
            for (size_t index = 0; index < arrsize(requests); index += 1) {
                if (requests[index].route(patcher, id, slide, size)) { probeMask |= 1U << index; }
            }
            gfxDiag->record(GfxDiagTag::Boot, 0x2000, probeMask, 0);
            SYSLOG("X6000", "Reset probe route mask: 0x%X", probeMask);

            RouteRequestPlus queueRequests[] = {
                {"__ZN27AMDRadeonX6000_AMDHWChannel19submitCommandBufferEP30AMD_SUBMIT_COMMAND_BUFFER_INFO",
                    wrapChannelSubmit, this->orgQueueProbes[0]},
                {"__ZN27AMDRadeonX6000_AMDHWChannel26timeStampInterruptCallbackEP8OSObjectPv",
                    wrapChannelComplete, this->orgQueueProbes[1]},
                {"__ZN26AMDRadeonX6000_AMDHardware22signalGPUWorkSubmittedEv", wrapWorkSubmitted,
                    this->orgQueueProbes[2]},
            };
            UInt32 queueMask = 0;
            for (size_t index = 0; index < arrsize(queueRequests); index += 1) {
                if (queueRequests[index].route(patcher, id, slide, size)) { queueMask |= 1U << index; }
            }
            gfxDiag->record(GfxDiagTag::Boot, 0x3000, queueMask, 0);
            SYSLOG("X6000", "Queue probe route mask: 0x%X", queueMask);
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

// Records one reset-path probe event and asks for an asynchronous ring dump.
UInt64 X6000::runResetProbe(UInt32 probe, UInt64 a0, UInt64 a1, UInt64 a2, UInt64 a3, UInt64 a4, UInt64 a5) {
    auto *diag = NootRXMain::callback->getGfxDiag();
    diag->record(GfxDiagTag::ResetEvent, probe, static_cast<UInt32>(a0), static_cast<UInt32>(a1));
    diag->requestDump();

    const auto original = callback->orgResetProbes[probe];
    if (original == 0) { return 0; }

    typedef UInt64 (*ResetProbeFunc)(UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);
    return reinterpret_cast<ResetProbeFunc>(original)(a0, a1, a2, a3, a4, a5);
}

UInt64 X6000::wrapDumpAsicHangState(UInt64 a0, UInt64 a1, UInt64 a2, UInt64 a3, UInt64 a4, UInt64 a5) {
    return runResetProbe(0, a0, a1, a2, a3, a4, a5);
}

UInt64 X6000::wrapResetHardware(UInt64 a0, UInt64 a1, UInt64 a2, UInt64 a3, UInt64 a4, UInt64 a5) {
    return runResetProbe(1, a0, a1, a2, a3, a4, a5);
}

UInt64 X6000::wrapSignalTimeout(UInt64 a0, UInt64 a1, UInt64 a2, UInt64 a3, UInt64 a4, UInt64 a5) {
    return runResetProbe(2, a0, a1, a2, a3, a4, a5);
}

// Feeds one queue probe into the busy/idle tracker and forwards all argument registers.
UInt64 X6000::runQueueProbe(UInt32 probe, UInt64 a0, UInt64 a1, UInt64 a2, UInt64 a3, UInt64 a4, UInt64 a5) {
    auto *diag = NootRXMain::callback->getGfxDiag();
    if (probe == 0) { diag->channelSubmit(a0); }
    else if (probe == 1) { diag->channelComplete(a0); }
    else { diag->noteWorkSubmitted(); }

    const auto original = callback->orgQueueProbes[probe];
    if (original == 0) { return 0; }

    typedef UInt64 (*QueueProbeFunc)(UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);
    return reinterpret_cast<QueueProbeFunc>(original)(a0, a1, a2, a3, a4, a5);
}

UInt64 X6000::wrapChannelSubmit(UInt64 a0, UInt64 a1, UInt64 a2, UInt64 a3, UInt64 a4, UInt64 a5) {
    return runQueueProbe(0, a0, a1, a2, a3, a4, a5);
}

UInt64 X6000::wrapChannelComplete(UInt64 a0, UInt64 a1, UInt64 a2, UInt64 a3, UInt64 a4, UInt64 a5) {
    return runQueueProbe(1, a0, a1, a2, a3, a4, a5);
}

UInt64 X6000::wrapWorkSubmitted(UInt64 a0, UInt64 a1, UInt64 a2, UInt64 a3, UInt64 a4, UInt64 a5) {
    return runQueueProbe(2, a0, a1, a2, a3, a4, a5);
}
