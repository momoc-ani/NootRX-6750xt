// Copyright © 2023-2026 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#pragma once

#include <stdint.h>

namespace DDICapabilityPolicy {

constexpr uint32_t FeatureCapsIndex = 10;

// Returns the Apple donor table only for Tahoe Navi22; all existing paths retain the universal table.
inline const uint32_t *select(const uint32_t *donorCaps, const uint32_t *universalCaps, bool isTahoe,
    bool isNavi22) {
    return isTahoe && isNavi22 ? donorCaps : universalCaps;
}

}    // namespace DDICapabilityPolicy
