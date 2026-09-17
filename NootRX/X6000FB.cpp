// Copyright © 2023-2024 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#include "X6000FB.hpp"
#include "DDICapabilityPolicy.hpp"
#include "NootRX.hpp"
#include "PatcherPlus.hpp"
#include <Headers/kern_api.hpp>

static const char *pathRadeonX6000Framebuffer =
    "/System/Library/Extensions/AMDRadeonX6000Framebuffer.kext/Contents/MacOS/AMDRadeonX6000Framebuffer";

static KernelPatcher::KextInfo kextRadeonX6000Framebuffer {
    "com.apple.kext.AMDRadeonX6000Framebuffer",
    &pathRadeonX6000Framebuffer,
    1,
    {},
    {},
    KernelPatcher::KextInfo::Unloaded,
};

X6000FB *X6000FB::callback = nullptr;

void X6000FB::init() {
    SYSLOG("X6000FB", "Module initialised");

    callback = this;

    lilu.onKextLoadForce(&kextRadeonX6000Framebuffer);
}

bool X6000FB::processKext(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size) {
    if (kextRadeonX6000Framebuffer.loadIndex == id) {
        NootRXMain::callback->ensureRMMIO();

        CAILAsicCapsEntry *orgAsicCapsTable = nullptr;

        SolveRequestPlus solveRequest {"__ZL20CAIL_ASIC_CAPS_TABLE", orgAsicCapsTable, kCailAsicCapsTablePattern};
        PANIC_COND(!solveRequest.solve(patcher, id, slide, size), "X6000FB", "Failed to resolve CAIL_ASIC_CAPS_TABLE");

        if (!NootRXMain::callback->attributes.isNavi21()) {
            RouteRequestPlus request {"__ZNK32AMDRadeonX6000_AmdAsicInfoNavi2327getEnumeratedRevisionNumberEv",
                wrapGetEnumeratedRevision};
            PANIC_COND(!request.route(patcher, id, slide, size), "X6000FB",
                "Failed to route getEnumeratedRevisionNumber");
        }

        if (ADDPR(debugEnabled) || NootRXMain::callback->isPowerDiagnosticsEnabled()) {
            RouteRequestPlus requests[] = {
                {"__ZN24AMDRadeonX6000_AmdLogger15initWithPciInfoEP11IOPCIDevice", wrapInitWithPciInfo,
                    this->orgInitWithPciInfo},
                {"__ZN34AMDRadeonX6000_AmdRadeonController10doGPUPanicEPKcz", wrapDoGPUPanic},
                {"_dm_logger_write", wrapDmLoggerWrite, kDmLoggerWritePattern},
            };
            PANIC_COND(!RouteRequestPlus::routeAll(patcher, id, requests, slide, size), "X6000FB",
                "Failed to route debug symbols");
        }

        // Locate a real Navi donor entry before changing the table. Tahoe's first
        // entry belongs to a different ASIC and must remain untouched.
        const UInt32 donorDeviceId = NootRXMain::callback->attributes.isNavi21() ? 0x73BF : 0x73FF;
        CAILAsicCapsEntry *donorEntry = nullptr;
        CAILAsicCapsEntry *fallbackEntry = nullptr;
        constexpr size_t cailAsicCapsTableEntries = 0x1FB;
        for (size_t index = 0; index < cailAsicCapsTableEntries; index++) {
            auto *entry = &orgAsicCapsTable[index];
            if (entry->familyId != AMDGPU_FAMILY_NAVI || entry->deviceId != donorDeviceId ||
                entry->revNo != NootRXMain::callback->devRevision) {
                continue;
            }
            if (fallbackEntry == nullptr) { fallbackEntry = entry; }
            if (entry->revId == NootRXMain::callback->pciRevision || entry->revId == 0xFFFFFFFF) {
                donorEntry = entry;
                break;
            }
        }
        if (donorEntry == nullptr) { donorEntry = fallbackEntry; }
        PANIC_COND(donorEntry == nullptr, "X6000FB", "Failed to find ASIC caps donor entry for device 0x%04X",
            donorDeviceId);

        // Preserve Tahoe's Apple donor table for Navi22 while retaining the existing universal policy elsewhere.
        const auto *selectedCaps = DDICapabilityPolicy::select(donorEntry->caps, ddiCapsNavi2Universal,
            getKernelVersion() == KernelVersion::Tahoe, NootRXMain::callback->attributes.isNavi22());

        PANIC_COND(MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS, "X6000FB",
            "Failed to enable kernel writing");
        donorEntry->familyId = AMDGPU_FAMILY_NAVI;
        donorEntry->deviceId = NootRXMain::callback->deviceId;
        donorEntry->revNo = NootRXMain::callback->devRevision;
        donorEntry->emulatedRevNo =
            static_cast<UInt32>(NootRXMain::callback->enumRevision) + NootRXMain::callback->devRevision;
        donorEntry->revId = NootRXMain::callback->pciRevision;
        donorEntry->caps = selectedCaps;
        MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
        NootRXMain::callback->publishDDICapabilitySelection("NootRX_DDICaps_X6000FB", donorDeviceId,
            donorEntry->caps);
        DBGLOG("X6000FB", "Applied DDI Caps patch using donor 0x%04X", donorDeviceId);

        if (ADDPR(debugEnabled) || NootRXMain::callback->isPowerDiagnosticsEnabled()) {
            auto *logEnableMaskMinors =
                patcher.solveSymbol<void *>(id, "__ZN14AmdDalDmLogger19LogEnableMaskMinorsE", slide, size);
            patcher.clearError();

            if (logEnableMaskMinors == nullptr) {
                size_t offset = 0;
                PANIC_COND(!KernelPatcher::findPattern(kDalDmLoggerShouldLogPartialPattern,
                               kDalDmLoggerShouldLogPartialPatternMask, arrsize(kDalDmLoggerShouldLogPartialPattern),
                               reinterpret_cast<const void *>(slide), size, &offset),
                    "X6000FB", "Failed to solve LogEnableMaskMinors");
                auto *instAddr = reinterpret_cast<UInt8 *>(slide + offset);
                // inst + instSize + imm32 = addr
                logEnableMaskMinors = instAddr + 7 + *reinterpret_cast<SInt32 *>(instAddr + 3);
            }

            PANIC_COND(MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS, "X6000FB",
                "Failed to enable kernel writing");
            memset(logEnableMaskMinors, 0xFF, 0x80);    // Enable all DalDmLogger logs
            MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);

            // Enable all Display Core and BiosParserHelper logs
            const LookupPatchPlus patches[] = {
                {&kextRadeonX6000Framebuffer, kInitPopulateDcInitDataOriginal, kInitPopulateDcInitDataPatched, 1},
                {&kextRadeonX6000Framebuffer, kBiosParserHelperInitWithDataOriginal,
                    kBiosParserHelperInitWithDataPatched, 1},
            };
            PANIC_COND(!LookupPatchPlus::applyAll(patcher, patches, slide, size), "X6000FB",
                "Failed to apply debug enablement patches");
        }

        return true;
    }

    return false;
}

UInt32 X6000FB::wrapGetEnumeratedRevision(void *) { return NootRXMain::callback->enumRevision; }

bool X6000FB::wrapInitWithPciInfo(void *that, void *pciDevice) {
    auto ret = FunctionCast(wrapInitWithPciInfo, callback->orgInitWithPciInfo)(that, pciDevice);
    getMember<UInt64>(that, 0x28) = 0xFFFFFFFFFFFFFFFF;    // Enable all log types
    getMember<UInt32>(that, 0x30) = 0xFF;                  // Enable all log severities
    return ret;
}

// Records the original GPU panic text together with the active RX 6750 XT profile before panicking.
void X6000FB::wrapDoGPUPanic(void *, char const *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    auto *buf = static_cast<char *>(IOMalloc(1000));
    bzero(buf, 1000);
    vsnprintf(buf, 1000, fmt, va);
    va_end(va);

    const auto &profile = NootRXMain::callback->getPowerProfile();
    SYSLOG("X6000FB", "GPU panic: device=0x%04X pciRev=0x%X profile=0x%X ULV=%u GFXOFF=%u FalconQuick=%u WorkLoadMask=%u message=%s",
        NootRXMain::callback->deviceId, NootRXMain::callback->pciRevision, profile.overrideMask,
        profile.disableULV, profile.gfxOffControl, profile.falconQuickTransition, profile.workLoadPolicyMask, buf);
    DBGLOG("X6000FB", "doGPUPanic: %s", buf);
    IOSleep(10000);
    panic("%s", buf);
}

constexpr static const char *LogTypes[] = {
    "Error",
    "Warning",
    "Debug",
    "DC_Interface",
    "DTN",
    "Surface",
    "HW_Hotplug",
    "HW_LKTN",
    "HW_Mode",
    "HW_Resume",
    "HW_Audio",
    "HW_HPDIRQ",
    "MST",
    "Scaler",
    "BIOS",
    "BWCalcs",
    "BWValidation",
    "I2C_AUX",
    "Sync",
    "Backlight",
    "Override",
    "Edid",
    "DP_Caps",
    "Resource",
    "DML",
    "Mode",
    "Detect",
    "LKTN",
    "LinkLoss",
    "Underflow",
    "InterfaceTrace",
    "PerfTrace",
    "DisplayStats",
};

// Returns true when a DAL message contains a marker associated with a GPU reset or hang.
static bool containsPowerFailureMarker(const char *message) {
    if (message == nullptr) { return false; }

    return strstr(message, "GFX is hung") != nullptr || strstr(message, "GPU Reset") != nullptr ||
           strstr(message, "GPU reset") != nullptr || strstr(message, "watchdog") != nullptr ||
           strstr(message, "Watchdog") != nullptr || strstr(message, "channel") != nullptr;
}

// Needed to prevent stack overflow
void X6000FB::wrapDmLoggerWrite(void *, const UInt32 logType, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    auto *message = static_cast<char *>(IOMalloc(0x1000));
    if (message == nullptr) {
        va_end(va);
        SYSLOG("X6000FB", "DAL logger message allocation failed (type=%u)", logType);
        return;
    }
    vsnprintf(message, 0x1000, fmt, va);
    va_end(va);
    const auto messageLength = strnlen(message, 0x1000);
    auto *epilogue = messageLength > 0 && message[messageLength - 1] == '\n' ? "" : "\n";
    if (logType < arrsize(LogTypes)) {
        kprintf("[%s]\t%s%s", LogTypes[logType], message, epilogue);
    } else {
        kprintf("%s%s", message, epilogue);
    }
    if (NootRXMain::callback != nullptr && NootRXMain::callback->isPowerDiagnosticsEnabled() &&
        containsPowerFailureMarker(message)) {
        SYSLOG("X6000FB", "DAL failure marker: type=%u message=%s", logType, message);
    }
    IOFree(message, 0x1000);
}
