// Copyright © 2023-2024 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#pragma once
#include "DCCDiagnosticsPolicy.hpp"
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_util.hpp>
#include <IOKit/IOService.h>
#include <IOKit/graphics/IOGraphicsTypes.h>

class X6000 {
    static X6000 *callback;

    public:
    void init();
    bool processKext(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size);

    private:
    using AlignManagerInitFunction = IOReturn (*)(void *, void *);
    using ShouldAllocScanoutDccFunction = bool (*)(void *, UInt32, UInt32, UInt32, UInt32);
    using GetDccInfo2Function = IOReturn (*)(void *, const AppleAddr2ComputeDccInfoInputV1 *,
        AppleAddr2ComputeDccInfoOutputV1 *);

    mach_vm_address_t orgGetHWInfo {0};
    AlignManagerInitFunction orgAlignManagerInit {nullptr};
    ShouldAllocScanoutDccFunction orgShouldAllocScanoutDcc {nullptr};
    GetDccInfo2Function orgGetDccInfo2 {nullptr};

    // Calls the original video-context query before applying the existing device-ID compatibility value.
    static IOReturn wrapGetHWInfo(IOService *accelVideoCtx, void *hwInfo);

    // Calls the original AddrLib initialiser and records the exact create identity getters.
    static IOReturn wrapAlignManagerInit(void *that, void *hardwareInterface);

    // Calls the original scanout decision and records its unmodified inputs and result.
    static bool wrapShouldAllocScanoutDcc(void *that, UInt32 width, UInt32 height, UInt32 candidateFlags,
        UInt32 pixelFormatSelector);

    // Calls the original AddrLib DCC computation and records its unmodified input and output.
    static IOReturn wrapGetDccInfo2(void *that, const AppleAddr2ComputeDccInfoInputV1 *input,
        AppleAddr2ComputeDccInfoOutputV1 *output);
};

//------ Patches ------//

// Fix flags enabled for Navi 22 inside HwlConvertChipFamily
static const UInt8 kHwlConvertChipFamilyOriginal[] = {0x8D, 0x4A, 0xD8, 0x83, 0xF9, 0x09, 0x76, 0x00, 0x83, 0xC2, 0xC4,
    0x83, 0xFA, 0x09, 0x77, 0x00, 0x48, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0xF5, 0xFF, 0xFF, 0xFF, 0x48, 0x21, 0xC8};
static const UInt8 kHwlConvertChipFamilyOriginalMask[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const UInt8 kHwlConvertChipFamilyPatched[] = {0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66,
    0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x90};
