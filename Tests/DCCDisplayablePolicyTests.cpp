#include "../NootRX/DCCDisplayablePolicy.hpp"

#include <cassert>
#include <cstdint>

// Verifies that installing the kext without the experiment argument preserves the injected XML.
static void testStableDefaultDoesNotOverride() {
    const auto policy = GPUDCCDisplayablePolicy::stable();

    assert(policy.value == 1);
    assert(!policy.overridden);
}

// Verifies that only Tahoe with the exact RX 6750 XT PCI identity enters the experiment path.
static void testTargetPlatformGate() {
    assert(GPUDCCDisplayablePolicy::isTarget(true, 0x73DF, 0xC0));
    assert(!GPUDCCDisplayablePolicy::isTarget(false, 0x73DF, 0xC0));
    assert(!GPUDCCDisplayablePolicy::isTarget(true, 0x73BF, 0xC0));
    assert(!GPUDCCDisplayablePolicy::isTarget(true, 0x73DF, 0xC1));
}

// Verifies that zero explicitly disables the displayable DCC property.
static void testDisableOverride() {
    auto policy = GPUDCCDisplayablePolicy::stable();

    assert(policy.overrideValue(0));
    assert(policy.value == 0);
    assert(policy.overridden);
}

// Verifies that one can explicitly retain the current displayable DCC property.
static void testEnableOverride() {
    auto policy = GPUDCCDisplayablePolicy::stable();

    assert(policy.overrideValue(1));
    assert(policy.value == 1);
    assert(policy.overridden);
}

// Verifies that unsupported values leave the stable configuration untouched.
static void testInvalidOverrideIsIgnored() {
    auto policy = GPUDCCDisplayablePolicy::stable();

    assert(!policy.overrideValue(2));
    assert(policy.value == 1);
    assert(!policy.overridden);
}

// Runs the dependency-free DCC displayable configuration tests on the build host.
int main() {
    testStableDefaultDoesNotOverride();
    testTargetPlatformGate();
    testDisableOverride();
    testEnableOverride();
    testInvalidOverrideIsIgnored();
    return 0;
}
