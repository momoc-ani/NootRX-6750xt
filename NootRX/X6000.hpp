// Copyright © 2023-2024 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#pragma once
#include "DiagPolicy.hpp"
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
    mach_vm_address_t orgGetHWInfo {0};
#if NOOTRX_GFX_DIAG
    mach_vm_address_t orgResetProbes[3] {};
    mach_vm_address_t orgQueueProbes[3] {};
#endif

    static IOReturn wrapGetHWInfo(IOService *accelVideoCtx, void *hwInfo);

#if NOOTRX_GFX_DIAG
    // Read-only probes around the accelerator reset and hang-detection paths.
    static UInt64 wrapDumpAsicHangState(UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);
    static UInt64 wrapResetHardware(UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);
    static UInt64 wrapSignalTimeout(UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);
    static UInt64 runResetProbe(UInt32 probe, UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);

    // Queue submission/completion probes feeding the busy/idle transition tracker.
    static UInt64 wrapChannelSubmit(UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);
    static UInt64 wrapChannelComplete(UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);
    static UInt64 wrapWorkSubmitted(UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);
    static UInt64 runQueueProbe(UInt32 probe, UInt64, UInt64, UInt64, UInt64, UInt64, UInt64);

    // Reads the driver's GC hang register list once at hang time and records every value.
    static void captureHangRegisters();

    // Replaces the Navi23 PM4 engine "execution point" stub so the hang report marks the stalled packet.
    static void installExecutionPointLocator(KernelPatcher &patcher, size_t id);

    // Returns the CP IB1 consumed dword count when the reported buffer is the one IB1 executes, else -1.
    static UInt32 wrapGetExecutionPoint(void *engine, void *hwChannel, UInt64 gpuAddress, UInt32 sizeDwords);
#endif    // NOOTRX_GFX_DIAG
};

//------ Patches ------//

// Fix flags enabled for Navi 22 inside HwlConvertChipFamily
static const UInt8 kHwlConvertChipFamilyOriginal[] = {0x8D, 0x4A, 0xD8, 0x83, 0xF9, 0x09, 0x76, 0x00, 0x83, 0xC2, 0xC4,
    0x83, 0xFA, 0x09, 0x77, 0x00, 0x48, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0xF5, 0xFF, 0xFF, 0xFF, 0x48, 0x21, 0xC8};
static const UInt8 kHwlConvertChipFamilyOriginalMask[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#if NOOTRX_GFX_DIAG
// GC register segment bases (MMIO dword offsets) used by the 10.3 register lists.
static constexpr UInt32 GcSegmentBase[2] = {0x1260, 0xA000};
// CP IB1 registers read by the driver's own ring report (segment 1): IB1 base low/high,
// programmed size and remaining size, all in dwords.
static constexpr UInt32 mmCP_IB1_BASE_LO = 0xA000 + 0x20CC;
static constexpr UInt32 mmCP_IB1_BASE_HI = 0xA000 + 0x20CD;
static constexpr UInt32 mmCP_IB1_BUFSZ = 0xA000 + 0x20C0;
static constexpr UInt32 mmCP_IB1_BUFSZ_REMAIN = 0xA000 + 0x20CE;
// Byte offset of the execution-point slot inside AMDNavi23PM4Engine's vtable (0x1E0 past the address point).
static constexpr size_t kExecutionPointVtableOffset = 0x10 + 0x1E0;
// Body of the original slot: push rbp; mov rbp, rsp; mov eax, -1; pop rbp; ret.
static const UInt8 kExecutionPointStub[] = {0x55, 0x48, 0x89, 0xE5, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x5D, 0xC3};
#endif

static const UInt8 kHwlConvertChipFamilyPatched[] = {0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66,
    0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x66, 0x90, 0x90};
