// Copyright © 2023-2024 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#pragma once
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_util.hpp>

class X6000FB {
    static X6000FB *callback;

    public:
    void init();
    bool processKext(KernelPatcher &patcher, size_t id, mach_vm_address_t slide, size_t size);

    private:
    using CallPlatformFunctionFromDrvrFunction = IOReturn (*)(void *, UInt32, void *, void *, void *);

    mach_vm_address_t orgInitWithPciInfo {0};
    CallPlatformFunctionFromDrvrFunction orgCallPlatformFunctionFromDrvr {nullptr};

    // Returns the existing enumerated revision selected for the active GPU family.
    static UInt32 wrapGetEnumeratedRevision(void *that);

    // Calls the original logger initialiser before enabling the existing diagnostic log masks.
    static bool wrapInitWithPciInfo(void *that, void *pciDevice);

    // Records the original GPU panic message and active PowerPlay profile before panicking.
    static void wrapDoGPUPanic(void *that, char const *fmt, ...);

    // Forwards one DAL log message while retaining failure markers needed for reset diagnosis.
    static void wrapDmLoggerWrite(void *logger, const UInt32 logType, const char *fmt, ...);

    // Records request 0x1A around the original Framebuffer handler without changing the request.
    static IOReturn wrapCallPlatformFunctionFromDrvr(void *that, UInt32 requestType, void *param1,
        void *param2, void *param3);
};

//------ Patterns ------//

static const UInt8 kCailAsicCapsTablePattern[] = {0x6E, 0x00, 0x00, 0x00, 0x98, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};

static const UInt8 kDmLoggerWritePattern[] = {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54,
    0x53, 0x48, 0x81, 0xEC, 0x88, 0x04, 0x00, 0x00};

static const UInt8 kDalDmLoggerShouldLogPartialPattern[] = {0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x04, 0x81,
    0x0F, 0xA3, 0xD0, 0x0F, 0x92, 0xC0};
static const UInt8 kDalDmLoggerShouldLogPartialPatternMask[] = {0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

//------ Patches ------//

// Enable all Display Core logs
static const UInt8 kInitPopulateDcInitDataOriginal[] = {0x48, 0xB9, 0xDB, 0x1B, 0xFF, 0x7E, 0x10, 0x00, 0x00, 0x00};
static const UInt8 kInitPopulateDcInitDataPatched[] = {0x48, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Enable all AmdBiosParserHelper logs
static const UInt8 kBiosParserHelperInitWithDataOriginal[] = {0x08, 0xC7, 0x07, 0x01, 0x00, 0x00, 0x00};
static const UInt8 kBiosParserHelperInitWithDataPatched[] = {0x08, 0xC7, 0x07, 0xFF, 0x00, 0x00, 0x00};
