#include "../NootRX/PowerProfile.hpp"

#include <cassert>

// Verifies that installing the new kext without boot arguments preserves the current stable baseline.
static void testStableDefaults() {
    const auto profile = RX6750XTPowerProfile::stable();

    assert(profile.disableULV == 1);
    assert(profile.gfxOffControl == 0);
    assert(profile.falconQuickTransition == 0);
    assert(profile.workLoadPolicyMask == 0);
    assert(profile.overrideMask == 0);
}

// Verifies the upstream personality values used when all four power-management paths are restored.
static void testUpstreamDefaults() {
    const auto profile = RX6750XTPowerProfile::upstream();

    assert(profile.disableULV == 0);
    assert(profile.gfxOffControl == 1);
    assert(profile.falconQuickTransition == 1);
    assert(profile.workLoadPolicyMask == 16);
    assert(profile.overrideMask == 0);
}

// Verifies that each experimental value can be changed without modifying the other three values.
static void testIndependentOverrides() {
    auto profile = RX6750XTPowerProfile::stable();

    profile.overrideValue(RX6750XTPowerSetting::DisableULV, 0);
    assert(profile.disableULV == 0);
    assert(profile.gfxOffControl == 0);
    assert(profile.falconQuickTransition == 0);
    assert(profile.workLoadPolicyMask == 0);
    assert(profile.overrideMask == 1U);

    profile.overrideValue(RX6750XTPowerSetting::GfxOffControl, 1);
    assert(profile.disableULV == 0);
    assert(profile.gfxOffControl == 1);
    assert(profile.falconQuickTransition == 0);
    assert(profile.workLoadPolicyMask == 0);
    assert(profile.overrideMask == 3U);

    profile.overrideValue(RX6750XTPowerSetting::FalconQuickTransition, 1);
    assert(profile.disableULV == 0);
    assert(profile.gfxOffControl == 1);
    assert(profile.falconQuickTransition == 1);
    assert(profile.workLoadPolicyMask == 0);
    assert(profile.overrideMask == 7U);

    profile.overrideValue(RX6750XTPowerSetting::WorkLoadPolicyMask, 16);
    assert(profile.disableULV == 0);
    assert(profile.gfxOffControl == 1);
    assert(profile.falconQuickTransition == 1);
    assert(profile.workLoadPolicyMask == 16);
    assert(profile.overrideMask == 15U);
}

// Verifies every four-bit combination used by the ordered field test plan.
static void testAllCombinationMasks() {
    for (uint32_t mask = 0; mask < 16; mask++) {
        auto profile = RX6750XTPowerProfile::stable();
        if ((mask & (1U << 0)) != 0) { profile.overrideValue(RX6750XTPowerSetting::DisableULV, 0); }
        if ((mask & (1U << 1)) != 0) { profile.overrideValue(RX6750XTPowerSetting::GfxOffControl, 1); }
        if ((mask & (1U << 2)) != 0) { profile.overrideValue(RX6750XTPowerSetting::FalconQuickTransition, 1); }
        if ((mask & (1U << 3)) != 0) { profile.overrideValue(RX6750XTPowerSetting::WorkLoadPolicyMask, 16); }
        assert(profile.overrideMask == mask);
    }
}

// Runs the dependency-free PowerPlay configuration tests on the build host.
int main() {
    testStableDefaults();
    testUpstreamDefaults();
    testIndependentOverrides();
    testAllCombinationMasks();
    return 0;
}
