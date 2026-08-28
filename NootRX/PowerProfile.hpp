// Copyright © 2023-2024 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#pragma once

#include <stdint.h>

// Identifies one independently configurable RX 6750 XT PowerPlay property.
enum class RX6750XTPowerSetting : uint8_t {
    DisableULV = 0,
    GfxOffControl,
    FalconQuickTransition,
    WorkLoadPolicyMask,
};

// Boot arguments use the exact PowerPlay property names and values documented here.
static constexpr const char *kRX6750XTDisableULVArg = "nootrx-pp-disable-ulv";
static constexpr const char *kRX6750XTGfxOffControlArg = "nootrx-pp-gfxoff-control";
static constexpr const char *kRX6750XTFalconQuickTransitionArg = "nootrx-pp-falcon-quick-transition";
static constexpr const char *kRX6750XTWorkLoadPolicyMaskArg = "nootrx-pp-workload-policy-mask";

// Holds the effective PowerPlay values and records which values were overridden.
struct RX6750XTPowerProfile {
    uint32_t disableULV;
    uint32_t gfxOffControl;
    uint32_t falconQuickTransition;
    uint32_t workLoadPolicyMask;
    uint32_t overrideMask;

    // Returns the current stable profile used when no experiment boot-args are present.
    static constexpr RX6750XTPowerProfile stable() {
        return {1, 0, 0, 0, 0};
    }

    // Returns the original framebuffer personality values for an all-enabled experiment.
    static constexpr RX6750XTPowerProfile upstream() {
        return {0, 1, 1, 16, 0};
    }

    // Replaces one setting and marks only that setting as explicitly overridden.
    bool overrideValue(RX6750XTPowerSetting setting, uint32_t value) {
        const auto bit = 1U << static_cast<uint8_t>(setting);
        switch (setting) {
            case RX6750XTPowerSetting::DisableULV:
                disableULV = value;
                break;
            case RX6750XTPowerSetting::GfxOffControl:
                gfxOffControl = value;
                break;
            case RX6750XTPowerSetting::FalconQuickTransition:
                falconQuickTransition = value;
                break;
            case RX6750XTPowerSetting::WorkLoadPolicyMask:
                workLoadPolicyMask = value;
                break;
            default:
                return false;
        }

        overrideMask |= bit;
        return true;
    }
};
