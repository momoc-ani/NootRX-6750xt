#include "../NootRX/DCCDiagnosticsPolicy.hpp"

#include <cassert>
#include <cstddef>

// Verifies the exact AddrLib2 v1 input layout used by Tahoe build 25E253.
static void testAddrLibDccInputLayout() {
    static_assert(sizeof(AppleAddr2ComputeDccInfoInputV1) == 0x34);
    static_assert(offsetof(AppleAddr2ComputeDccInfoInputV1, swizzleMode) == 0x10);
    static_assert(offsetof(AppleAddr2ComputeDccInfoInputV1, bpp) == 0x14);
    static_assert(offsetof(AppleAddr2ComputeDccInfoInputV1, unalignedWidth) == 0x18);
    static_assert(offsetof(AppleAddr2ComputeDccInfoInputV1, firstMipIdInTail) == 0x30);
}

// Verifies the exact AddrLib2 v1 output layout used by Tahoe build 25E253.
static void testAddrLibDccOutputLayout() {
    static_assert(sizeof(AppleAddr2ComputeDccInfoOutputV1) == 0x48);
    static_assert(offsetof(AppleAddr2ComputeDccInfoOutputV1, dccRamBaseAlign) == 0x04);
    static_assert(offsetof(AppleAddr2ComputeDccInfoOutputV1, pitch) == 0x0C);
    static_assert(offsetof(AppleAddr2ComputeDccInfoOutputV1, metaBlkSize) == 0x30);
    static_assert(offsetof(AppleAddr2ComputeDccInfoOutputV1, pMipInfo) == 0x40);
}

// Verifies the external accelerator-to-framebuffer DCC request layouts.
static void testFramebufferDccRequestLayout() {
    static_assert(sizeof(AppleDccCapsParametersV1) == 0x20);
    static_assert(offsetof(AppleDccCapsParametersV1, width) == 0x10);
    static_assert(sizeof(AppleDccCapabilitiesV1) == 0x10);
    static_assert(offsetof(AppleDccCapabilitiesV1, capability0) == 0x04);
    static_assert(offsetof(AppleDccCapabilitiesV1, field0) == 0x08);
}

// Runs all dependency-free DCC diagnostic policy tests.
int main() {
    testAddrLibDccInputLayout();
    testAddrLibDccOutputLayout();
    testFramebufferDccRequestLayout();
    return 0;
}
