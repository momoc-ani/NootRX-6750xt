// Copyright © 2023-2026 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#pragma once

#include <stddef.h>
#include <stdint.h>

// Long-run GFX/KIQ diagnostics policy. Every decision here is pure so it can be
// unit tested on the host, away from the kernel build environment.
namespace GfxDiagPolicy {

// Boot argument that enables the bounded GFX/KIQ observation recorder.
static constexpr const char *BootArg = "-NRXGfxDiag";
// Only the RX 6750 XT is instrumented; other GPUs keep the untouched upstream behaviour.
static constexpr uint32_t TargetDeviceId = 0x73DF;
// Fixed ring size; bounds both kernel memory use and the size of a failure-time flush.
static constexpr size_t RingCapacity = 1024;
// Event text kept per ring slot.
static constexpr size_t TextLength = 40;
// Bounded text size of one IORegistry dump snapshot.
static constexpr size_t DumpLength = 65536;
// Upper bound of DAL log type counters kept for the ambient distribution.
static constexpr size_t MaxLogTypes = 40;
// Upper bound of SMU message id counters kept for the ambient distribution.
static constexpr size_t MaxSmuMessages = 256;
// Heartbeat cadence in seconds; exactly one log line per interval.
static constexpr uint64_t HeartbeatSeconds = 120;
// Minimum interval between logger-triggered flushes, in seconds.
static constexpr uint64_t FlushCooldownSeconds = 300;

// Returns whether the explicit diagnostics request targets the supported GPU.
constexpr inline bool isTarget(bool requested, uint32_t deviceId) { return requested && deviceId == TargetDeviceId; }

// Returns true when the NUL-terminated haystack contains the NUL-terminated needle.
// Implemented locally because the kernel SDK does not export strstr().
inline bool containsSubstring(const char *haystack, const char *needle) {
    if (haystack == nullptr || needle == nullptr || *needle == '\0') { return false; }

    for (const char *start = haystack; *start != '\0'; start += 1) {
        const char *h = start;
        const char *n = needle;
        while (*h != '\0' && *n != '\0' && *h == *n) {
            h += 1;
            n += 1;
        }
        if (*n == '\0') { return true; }
    }

    return false;
}

// Returns true when one driver message contains a marker that can precede a fatal GPU event.
inline bool containsFailureMarker(const char *message) {
    if (message == nullptr) { return false; }

    static const char *const markers[] = {
        "GFX is hung", "GPU Reset", "GPU reset", "watchdog", "Watchdog", "KIQ", "RSMU", "not responding", "hang"};
    for (const auto *marker : markers) {
        if (containsSubstring(message, marker)) { return true; }
    }

    return false;
}

// Returns whether one DAL log type is kept in the ring. Other types only update counters,
// which keeps the ring focused on power, mode, link and error events instead of frame chatter.
inline bool isInterestingLogType(uint32_t logType) {
    switch (logType) {
        case 0:     // Error
        case 1:     // Warning
        case 6:     // HW_Hotplug
        case 7:     // HW_LKTN
        case 8:     // HW_Mode
        case 9:     // HW_Resume
        case 11:    // HW_HPDIRQ
        case 14:    // BIOS
        case 20:    // Override
        case 22:    // DP_Caps
        case 23:    // Resource
        case 25:    // Mode
        case 26:    // Detect
        case 27:    // LKTN
        case 28:    // LinkLoss
        case 29:    // Underflow
        case 32:    // DisplayStats
            return true;
        default:
            return false;
    }
}

}    // namespace GfxDiagPolicy

// Optional per-property PowerPlay overrides. Nothing is written unless a boot argument
// explicitly requests one field, so the injected personality stays upstream-equivalent.
namespace PowerOverridePolicy {

// One boot argument per PowerPlay property; values are the raw driver values.
static constexpr const char *DisableULVArg = "nootrx-pp-disable-ulv";
static constexpr const char *GfxOffControlArg = "nootrx-pp-gfxoff-control";
static constexpr const char *FalconQuickTransitionArg = "nootrx-pp-falcon-quick-transition";
static constexpr const char *WorkLoadPolicyMaskArg = "nootrx-pp-workload-policy-mask";

enum class Field : size_t { DisableULV = 0, GfxOffControl, FalconQuickTransition, WorkLoadPolicyMask, Count };

// Holds the requested overrides; a zero mask means the personality stays untouched.
struct Profile {
    uint32_t values[static_cast<size_t>(Field::Count)] {0, 1, 1, 16};
    uint32_t overrideMask {0};

    // Stores one requested value and marks only that field as explicitly overridden.
    bool set(Field field, uint32_t value) {
        const auto index = static_cast<size_t>(field);
        if (index >= static_cast<size_t>(Field::Count)) { return false; }
        this->values[index] = value;
        this->overrideMask |= 1U << index;
        return true;
    }

    // Returns whether at least one property must be written into the personality.
    bool any() const { return this->overrideMask != 0; }

    // Returns whether one field was explicitly requested through a boot argument.
    bool overridden(Field field) const { return (this->overrideMask & (1U << static_cast<size_t>(field))) != 0; }

    // Returns the requested value of one field.
    uint32_t value(Field field) const { return this->values[static_cast<size_t>(field)]; }
};

}    // namespace PowerOverridePolicy
