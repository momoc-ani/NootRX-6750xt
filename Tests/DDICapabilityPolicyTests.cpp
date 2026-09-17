#include "../NootRX/DDICapabilityPolicy.hpp"

#include <cassert>
#include <cstdint>

namespace {

constexpr uint32_t donorCaps[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x42000020};
constexpr uint32_t universalCaps[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x42040028};

// Verifies that Tahoe keeps the Apple donor capability table for Navi22 GPUs.
void testTahoeNavi22PreservesDonor() {
    const auto *selected = DDICapabilityPolicy::select(donorCaps, universalCaps, true, true);

    assert(selected == donorCaps);
    assert(selected[DDICapabilityPolicy::FeatureCapsIndex] == 0x42000020);
}

// Verifies that older macOS releases keep the existing universal capability behavior for Navi22 GPUs.
void testNonTahoeNavi22UsesUniversalCaps() {
    const auto *selected = DDICapabilityPolicy::select(donorCaps, universalCaps, false, true);

    assert(selected == universalCaps);
    assert(selected[DDICapabilityPolicy::FeatureCapsIndex] == 0x42040028);
}

// Verifies that Tahoe does not change the capability policy for non-Navi22 GPUs.
void testTahoeNonNavi22UsesUniversalCaps() {
    const auto *selected = DDICapabilityPolicy::select(donorCaps, universalCaps, true, false);

    assert(selected == universalCaps);
    assert(selected[DDICapabilityPolicy::FeatureCapsIndex] == 0x42040028);
}

}    // namespace

// Runs all dependency-free DDI capability policy tests on the build host.
int main() {
    testTahoeNavi22PreservesDonor();
    testNonTahoeNavi22UsesUniversalCaps();
    testTahoeNonNavi22UsesUniversalCaps();
    return 0;
}
