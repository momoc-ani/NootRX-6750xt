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

// Verifies that build-specific diagnostics cannot affect another OS build, GPU, revision, or boot mode.
static void testDiagnosticTargetGate() {
    assert(DCCDiagnosticsPolicy::isTarget(true, "25E253", 0x73DF, 0xC0, true));
    assert(!DCCDiagnosticsPolicy::isTarget(true, "25E252", 0x73DF, 0xC0, true));
    assert(!DCCDiagnosticsPolicy::isTarget(true, "25E254", 0x73DF, 0xC0, true));
    assert(!DCCDiagnosticsPolicy::isTarget(true, nullptr, 0x73DF, 0xC0, true));
    assert(!DCCDiagnosticsPolicy::isTarget(false, "25E253", 0x73DF, 0xC0, true));
    assert(!DCCDiagnosticsPolicy::isTarget(true, "25E253", 0x73FF, 0xC0, true));
    assert(!DCCDiagnosticsPolicy::isTarget(true, "25E253", 0x73DF, 0xC1, true));
    assert(!DCCDiagnosticsPolicy::isTarget(true, "25E253", 0x73DF, 0xC0, false));
}

// Verifies that an exact successful duplicate is counted but not logged twice.
static void testExactDuplicateIsSuppressed() {
    DCCObservationCache cache;
    const DCCObservationKey key {DCCDiagnosticStage::ScanoutDecision, {2560, 1440, 1, 32}, 4};
    const auto first = cache.observe(key, false);
    const auto duplicate = cache.observe(key, false);

    assert(first.shouldLog);
    assert(first.sequence == 1);
    assert(!duplicate.shouldLog);
    assert(duplicate.sequence == 1);
    assert(cache.duplicateCount() == 1);
}

// Verifies that changed output fields form a new observation without a hash.
static void testChangedMetadataIsLogged() {
    DCCObservationCache cache;
    const DCCObservationKey first {DCCDiagnosticStage::AddrLibDccInfo, {2560, 1440, 256, 64}, 4};
    const DCCObservationKey changed {DCCDiagnosticStage::AddrLibDccInfo, {2560, 1440, 512, 64}, 4};

    assert(cache.observe(first, false).shouldLog);
    const auto decision = cache.observe(changed, false);
    assert(decision.shouldLog);
    assert(decision.sequence == 2);
}

// Verifies that every failed call remains visible even when its fields repeat.
static void testRepeatedFailureIsAlwaysLogged() {
    DCCObservationCache cache;
    const DCCObservationKey failure {DCCDiagnosticStage::FramebufferCapability, {1, 0xE00002C2}, 2};

    assert(cache.observe(failure, true).shouldLog);
    const auto repeated = cache.observe(failure, true);
    assert(repeated.shouldLog);
    assert(repeated.sequence == 2);
    assert(cache.errorCount() == 2);
}

// Verifies that a full fixed cache reports overflow once without allocating or spamming logs.
static void testCapacityOverflowIsBounded() {
    DCCObservationCache cache;
    for (uint32_t value = 0; value < DCCObservationCache::Capacity; value += 1) {
        const DCCObservationKey key {DCCDiagnosticStage::ScanoutDecision, {value}, 1};
        assert(cache.observe(key, false).shouldLog);
    }

    const DCCObservationKey firstOverflow {DCCDiagnosticStage::ScanoutDecision, {0x100}, 1};
    const DCCObservationKey repeatedOverflow {DCCDiagnosticStage::ScanoutDecision, {0x101}, 1};
    assert(cache.observe(firstOverflow, false).shouldLog);
    assert(!cache.observe(repeatedOverflow, false).shouldLog);
    assert(cache.overflowCount() == 2);
}

// Runs all dependency-free DCC diagnostic policy tests.
int main() {
    testAddrLibDccInputLayout();
    testAddrLibDccOutputLayout();
    testFramebufferDccRequestLayout();
    testDiagnosticTargetGate();
    testExactDuplicateIsSuppressed();
    testChangedMetadataIsLogged();
    testRepeatedFailureIsAlwaysLogged();
    testCapacityOverflowIsBounded();
    return 0;
}
