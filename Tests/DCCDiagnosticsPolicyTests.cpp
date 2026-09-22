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

// Verifies that the two runtime gate observations preserve the raw request and exact build comparison.
static void testDiagnosticGateObservation() {
    const auto matching = DCCDiagnosticsPolicy::observeGate("25E253", true);
    assert(matching.diagnosticsRequested);
    assert(matching.osBuildMatch);

    const auto requestMissing = DCCDiagnosticsPolicy::observeGate("25E253", false);
    assert(!requestMissing.diagnosticsRequested);
    assert(requestMissing.osBuildMatch);

    const auto buildMismatch = DCCDiagnosticsPolicy::observeGate("25E252", true);
    assert(buildMismatch.diagnosticsRequested);
    assert(!buildMismatch.osBuildMatch);

    const auto buildMissing = DCCDiagnosticsPolicy::observeGate(nullptr, true);
    assert(buildMissing.diagnosticsRequested);
    assert(!buildMissing.osBuildMatch);
}

// Verifies that successful duplicates publish only at logarithmic repeat checkpoints without logging.
static void testSuccessfulDuplicateUsesLogarithmicPublishCheckpoints() {
    DCCObservationCache cache;
    const DCCObservationKey key {DCCDiagnosticStage::ScanoutDecision, {2560, 1440, 1, 32}, 4};
    const auto first = cache.observe(key, false);
    const auto repeat1 = cache.observe(key, false);
    const auto repeat2 = cache.observe(key, false);
    const auto repeat3 = cache.observe(key, false);
    const auto repeat4 = cache.observe(key, false);

    assert(first.shouldLog);
    assert(first.shouldPublish);
    assert(first.sequence == 1);
    assert(first.repeatCount == 0);
    assert(!repeat1.shouldLog && repeat1.shouldPublish);
    assert(repeat1.sequence == 2 && repeat1.repeatCount == 1);
    assert(!repeat2.shouldLog && repeat2.shouldPublish);
    assert(repeat2.sequence == 3 && repeat2.repeatCount == 2);
    assert(!repeat3.shouldLog && !repeat3.shouldPublish);
    assert(repeat3.sequence == 3 && repeat3.repeatCount == 3);
    assert(!repeat4.shouldLog && repeat4.shouldPublish);
    assert(repeat4.sequence == 4 && repeat4.repeatCount == 4);
    assert(cache.duplicateCount() == 4);
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

// Verifies that repeated failures remain visible at bounded logarithmic checkpoints.
static void testRepeatedFailureUsesLogarithmicCheckpoints() {
    DCCObservationCache cache;
    const DCCObservationKey failure {DCCDiagnosticStage::FramebufferCapability, {1, 0xE00002C2}, 2};

    const auto first = cache.observe(failure, true);
    const auto repeat1 = cache.observe(failure, true);
    const auto repeat2 = cache.observe(failure, true);
    const auto repeat3 = cache.observe(failure, true);
    const auto repeat4 = cache.observe(failure, true);

    assert(first.shouldLog && first.shouldPublish && first.sequence == 1);
    assert(repeat1.shouldLog && repeat1.shouldPublish && repeat1.sequence == 2 && repeat1.repeatCount == 1);
    assert(repeat2.shouldLog && repeat2.shouldPublish && repeat2.sequence == 3 && repeat2.repeatCount == 2);
    assert(!repeat3.shouldLog && !repeat3.shouldPublish && repeat3.sequence == 3 && repeat3.repeatCount == 3);
    assert(repeat4.shouldLog && repeat4.shouldPublish && repeat4.sequence == 4 && repeat4.repeatCount == 4);
    assert(cache.duplicateCount() == 4);
    assert(cache.errorCount() == 5);
}

// Verifies that each diagnostic stage has an independent fixed-capacity observation cache.
static void testStageCapacitiesAreIndependent() {
    DCCObservationCache cache;
    for (uint32_t value = 0; value < DCCObservationCache::Capacity; value += 1) {
        const DCCObservationKey key {DCCDiagnosticStage::ScanoutDecision, {value}, 1};
        assert(cache.observe(key, false).shouldLog);
    }

    const DCCObservationKey framebuffer {DCCDiagnosticStage::FramebufferCapability, {0x200}, 1};
    const auto framebufferDecision = cache.observe(framebuffer, false);
    assert(framebufferDecision.shouldLog);
    assert(!framebufferDecision.overflow);
}

// Verifies that one full stage reports overflow only at logarithmic checkpoints.
static void testCapacityOverflowUsesLogarithmicCheckpoints() {
    DCCObservationCache cache;
    for (uint32_t value = 0; value < DCCObservationCache::Capacity; value += 1) {
        const DCCObservationKey key {DCCDiagnosticStage::ScanoutDecision, {value}, 1};
        assert(cache.observe(key, false).shouldLog);
    }

    const DCCObservationKey firstOverflow {DCCDiagnosticStage::ScanoutDecision, {0x100}, 1};
    const DCCObservationKey secondOverflow {DCCDiagnosticStage::ScanoutDecision, {0x101}, 1};
    const DCCObservationKey thirdOverflow {DCCDiagnosticStage::ScanoutDecision, {0x102}, 1};
    const DCCObservationKey fourthOverflow {DCCDiagnosticStage::ScanoutDecision, {0x103}, 1};
    const auto overflow1 = cache.observe(firstOverflow, false);
    const auto overflow2 = cache.observe(secondOverflow, false);
    const auto overflow3 = cache.observe(thirdOverflow, false);
    const auto overflow4 = cache.observe(fourthOverflow, false);

    assert(overflow1.shouldLog && overflow1.shouldPublish && overflow1.overflow && overflow1.repeatCount == 1);
    assert(overflow2.shouldLog && overflow2.shouldPublish && overflow2.overflow && overflow2.repeatCount == 2);
    assert(!overflow3.shouldLog && !overflow3.shouldPublish && overflow3.overflow && overflow3.repeatCount == 3);
    assert(overflow4.shouldLog && overflow4.shouldPublish && overflow4.overflow && overflow4.repeatCount == 4);
    assert(cache.overflowCount() == 4);
}

// Runs all dependency-free DCC diagnostic policy tests.
int main() {
    testAddrLibDccInputLayout();
    testAddrLibDccOutputLayout();
    testFramebufferDccRequestLayout();
    testDiagnosticTargetGate();
    testDiagnosticGateObservation();
    testSuccessfulDuplicateUsesLogarithmicPublishCheckpoints();
    testChangedMetadataIsLogged();
    testRepeatedFailureUsesLogarithmicCheckpoints();
    testStageCapacitiesAreIndependent();
    testCapacityOverflowUsesLogarithmicCheckpoints();
    return 0;
}
