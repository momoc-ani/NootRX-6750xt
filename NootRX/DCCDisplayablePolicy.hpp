// Copyright © 2023-2026 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#pragma once

#include <stdint.h>

static constexpr const char *kGPUDCCDisplayableArg = "nootrx-gpu-dcc-displayable";

struct GPUDCCDisplayablePolicy {
    uint32_t value;
    bool overridden;

    // Returns the current injected personality value without requesting a rewrite.
    static constexpr GPUDCCDisplayablePolicy stable() {
        return {1, false};
    }

    // Returns whether the experiment is allowed for this operating system and PCI identity.
    static constexpr bool isTarget(bool isTahoe, uint32_t deviceId, uint32_t pciRevision) {
        return isTahoe && deviceId == 0x73DF && pciRevision == 0xC0;
    }

    // Accepts only boolean values and records that the personality must be explicitly rewritten.
    bool overrideValue(uint32_t requestedValue) {
        if (requestedValue > 1) { return false; }
        value = requestedValue;
        overridden = true;
        return true;
    }
};
