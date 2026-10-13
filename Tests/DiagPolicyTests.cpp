// Copyright © 2023-2026 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#include "../NootRX/DiagPolicy.hpp"
#include <assert.h>
#include <stdio.h>

// Verifies the GFX/KIQ diagnostics gate and the failure-marker matching used for flushing.
static void testGfxDiagPolicy() {
    using namespace GfxDiagPolicy;

    assert(!isTarget(false, TargetDeviceId));
    assert(isTarget(true, TargetDeviceId));
    assert(!isTarget(true, 0x73FF));

    assert(!containsFailureMarker(nullptr));
    assert(!containsFailureMarker("normal driver message"));
    assert(containsFailureMarker("AMDRadeonX6000: GFX is hung"));
    assert(containsFailureMarker("GPU Reset failed"));
    assert(containsFailureMarker("Stamp Timeout for KIQ Submission"));
}

// Verifies that PowerPlay overrides stay inert until a boot argument requests a field.
static void testPowerOverridePolicy() {
    using namespace PowerOverridePolicy;

    Profile profile {};
    assert(!profile.any());
    assert(!profile.overridden(Field::DisableULV));
    assert(profile.value(Field::WorkLoadPolicyMask) == 16);

    assert(profile.set(Field::DisableULV, 1));
    assert(profile.any());
    assert(profile.overridden(Field::DisableULV));
    assert(!profile.overridden(Field::GfxOffControl));
    assert(profile.value(Field::DisableULV) == 1);
}

int main() {
    testGfxDiagPolicy();
    testPowerOverridePolicy();
    printf("DiagPolicyTests: all assertions passed\n");
    return 0;
}
