// Copyright © 2023-2026 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#include "GfxDiag.hpp"
#include "NootRX.hpp"
#include <Headers/kern_api.hpp>
#include <IOKit/pci/IOPCIDevice.h>
#include <kern/clock.h>
#include <libkern/c++/OSString.h>

#if NOOTRX_GFX_DIAG

// Converts whole seconds to absolute time units for thread call deadlines and rate limits.
static UInt64 secondsToAbsolute(UInt64 seconds) {
    UInt64 absolute = 0;
    nanoseconds_to_absolutetime(seconds * 1000000000ULL, &absolute);
    return absolute;
}

void GfxDiag::configure(IOPCIDevice *provider, bool enabled) {
    this->provider = provider;
    this->enabled = enabled;

    if (!enabled) { return; }

    if (provider != nullptr) {
        provider->setProperty("NootRX_GfxDiagEnabled", static_cast<UInt64>(1), 32);
        provider->setProperty("NootRX_GfxDiagEvents", static_cast<UInt64>(0), 32);
        provider->setProperty("NootRX_GfxDiagDropped", static_cast<UInt64>(0), 32);
    }

    this->lock = IOSimpleLockAlloc();
    if (this->lock == nullptr) {
        this->enabled = false;
        if (provider != nullptr) { provider->setProperty("NootRX_GfxDiagEnabled", static_cast<UInt64>(0), 32); }
        SYSLOG("GfxDiag", "Failed to allocate the diagnostics lock; diagnostics stay disabled");
        return;
    }

    this->heartbeatCall = thread_call_allocate_with_priority(heartbeatCallback, this, THREAD_CALL_PRIORITY_LOW);
    if (this->heartbeatCall != nullptr) {
        thread_call_enter_delayed(
            this->heartbeatCall, mach_absolute_time() + secondsToAbsolute(GfxDiagPolicy::HeartbeatSeconds));
    }
    this->dumpCall = thread_call_allocate_with_priority(dumpCallback, this, THREAD_CALL_PRIORITY_KERNEL);
    this->checkCall = thread_call_allocate_with_priority(stallCheckCallback, this, THREAD_CALL_PRIORITY_KERNEL);
    if (this->checkCall != nullptr) {
        thread_call_enter_delayed(this->checkCall, mach_absolute_time() + secondsToAbsolute(1));
    }

    this->publishDump("configure");

    SYSLOG("GfxDiag", "GFX diagnostics enabled: ring=%lu heartbeat=%llus flushCooldown=%llus",
        static_cast<unsigned long>(GfxDiagPolicy::RingCapacity), GfxDiagPolicy::HeartbeatSeconds,
        GfxDiagPolicy::FlushCooldownSeconds);
}

void GfxDiag::record(GfxDiagTag tag, UInt32 a, UInt32 b, UInt32 c, SInt32 result) {
    if (!this->enabled || this->lock == nullptr) { return; }

    const auto interruptState = IOSimpleLockLockDisableInterrupt(this->lock);
    auto &event = this->ring[this->writeIndex];
    event.seq = this->nextSeq++;
    event.uptimeSeconds = uptimeSeconds();
    event.tag = static_cast<UInt32>(tag);
    event.a = a;
    event.b = b;
    event.c = c;
    event.result = result;
    event.text[0] = '\0';
    this->writeIndex = (this->writeIndex + 1) % static_cast<UInt32>(GfxDiagPolicy::RingCapacity);
    if (this->storedCount < GfxDiagPolicy::RingCapacity) { this->storedCount += 1; }
    else { this->droppedCount += 1; }
    IOSimpleLockUnlockEnableInterrupt(this->lock, interruptState);
}

void GfxDiag::recordMessage(GfxDiagTag tag, const char *message, UInt32 a) {
    if (!this->enabled || this->lock == nullptr || message == nullptr) { return; }

    const auto interruptState = IOSimpleLockLockDisableInterrupt(this->lock);
    auto &event = this->ring[this->writeIndex];
    event.seq = this->nextSeq++;
    event.uptimeSeconds = uptimeSeconds();
    event.tag = static_cast<UInt32>(tag);
    event.a = a;
    event.b = 0;
    event.c = 0;
    event.result = 0;
    strncpy(event.text, message, GfxDiagPolicy::TextLength - 1);
    event.text[GfxDiagPolicy::TextLength - 1] = '\0';
    this->writeIndex = (this->writeIndex + 1) % static_cast<UInt32>(GfxDiagPolicy::RingCapacity);
    if (this->storedCount < GfxDiagPolicy::RingCapacity) { this->storedCount += 1; }
    else { this->droppedCount += 1; }
    IOSimpleLockUnlockEnableInterrupt(this->lock, interruptState);
}

void GfxDiag::disable(const char *reason) {
    if (!this->enabled) { return; }

    this->enabled = false;
    SYSLOG("GfxDiag", "GFX diagnostics disabled: %s", reason != nullptr ? reason : "(no reason)");
    if (this->provider != nullptr) {
        this->provider->setProperty("NootRX_GfxDiagEnabled", static_cast<UInt64>(0), 32);
    }
}

void GfxDiag::countMessageType(UInt32 logType) {
    if (!this->enabled || this->lock == nullptr || logType >= GfxDiagPolicy::MaxLogTypes) { return; }

    const auto interruptState = IOSimpleLockLockDisableInterrupt(this->lock);
    this->messageTypeCounters[logType] += 1;
    IOSimpleLockUnlockEnableInterrupt(this->lock, interruptState);
}

UInt32 GfxDiag::countSmuMessage(UInt32 msgId) {
    if (!this->enabled || this->lock == nullptr || msgId >= GfxDiagPolicy::MaxSmuMessages) { return 0; }

    const auto interruptState = IOSimpleLockLockDisableInterrupt(this->lock);
    const auto previous = this->smuMessageCounters[msgId];
    this->smuMessageCounters[msgId] = previous + 1;
    IOSimpleLockUnlockEnableInterrupt(this->lock, interruptState);
    return previous;
}

void GfxDiag::publishProbeMask(UInt32 mask) {
    this->probeMask = mask;
    if (this->provider != nullptr) {
        this->provider->setProperty("NootRX_GfxDiagProbeMask", static_cast<UInt64>(mask), 32);
    }
}

// Maps one channel object to a stable small index; kernel pointers never leave the kext.
UInt32 GfxDiag::channelIndexLocked(UInt64 channel) {
    if (channel == 0) { return 0xFFFFFFFFU; }
    for (UInt32 index = 0; index < ChannelSlots; index += 1) {
        if (this->channelKeys[index] == channel) { return index; }
    }
    for (UInt32 index = 0; index < ChannelSlots; index += 1) {
        if (this->channelKeys[index] == 0) {
            this->channelKeys[index] = channel;
            return index;
        }
    }
    return 0xFFFFFFFFU;
}

void GfxDiag::channelSubmit(UInt64 channel) {
    if (!this->enabled || this->lock == nullptr) { return; }

    const auto interruptState = IOSimpleLockLockDisableInterrupt(this->lock);
    const auto index = channelIndexLocked(channel);
    if (index != 0xFFFFFFFFU) {
        if (this->channelOutstanding[index] == 0) {
            this->channelPendingStart[index] = uptimeSeconds();
            this->channelSubmits[index] += 1;
        }
        this->channelOutstanding[index] += 1;
    }
    IOSimpleLockUnlockEnableInterrupt(this->lock, interruptState);
}

// The timestamp interrupt means the channel made progress: outstanding work is drained
// and the stall window restarts from this moment.
void GfxDiag::channelComplete(UInt64 channel) {
    if (!this->enabled || this->lock == nullptr) { return; }

    const auto now = uptimeSeconds();
    const auto interruptState = IOSimpleLockLockDisableInterrupt(this->lock);
    const auto index = channelIndexLocked(channel);
    UInt32 busySeconds = 0;
    if (index != 0xFFFFFFFFU && this->channelOutstanding[index] > 0) {
        if (this->channelPendingStart[index] != 0 && now > this->channelPendingStart[index]) {
            busySeconds = static_cast<UInt32>(now - this->channelPendingStart[index]);
        }
        this->channelOutstanding[index] = 0;
        this->channelPendingStart[index] = 0;
    }
    IOSimpleLockUnlockEnableInterrupt(this->lock, interruptState);

    // Only long-running jobs are reported; short submit/complete pairs stay uncounted.
    if (busySeconds >= 2) { this->record(GfxDiagTag::QueueEvent, index, busySeconds, 4); }
}

void GfxDiag::noteWorkSubmitted() {
    if (!this->enabled || this->lock == nullptr) { return; }

    const auto interruptState = IOSimpleLockLockDisableInterrupt(this->lock);
    this->workSubmittedCount += 1;
    IOSimpleLockUnlockEnableInterrupt(this->lock, interruptState);
}

void GfxDiag::publishDump(const char *reason) {
    if (this->provider == nullptr || this->lock == nullptr) { return; }

    const auto interruptState = IOSimpleLockLockDisableInterrupt(this->lock);
    const auto count = this->storedCount;
    const auto start = count == GfxDiagPolicy::RingCapacity ? this->writeIndex : 0;
    IOSimpleLockUnlockEnableInterrupt(this->lock, interruptState);

    size_t used = 0;
    used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used,
        "reason=%s seq=%llu uptime=%llus events=%u dropped=%u flushes=%u heartbeats=%u\n",
        reason != nullptr ? reason : "(none)", this->dumpSeq, uptimeSeconds(), count, this->droppedCount,
        this->flushCount, this->heartbeatCount));

    used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used,
        "probeMask=0x%X\n", this->probeMask));

    used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used, "types="));
    for (size_t type = 0; type < GfxDiagPolicy::MaxLogTypes && used < GfxDiagPolicy::DumpLength; type += 1) {
        if (this->messageTypeCounters[type] == 0) { continue; }
        used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used, "%zu:%u,",
            type, this->messageTypeCounters[type]));
    }
    used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used, "\n"));

    used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used, "smu="));
    for (size_t id = 0; id < GfxDiagPolicy::MaxSmuMessages && used < GfxDiagPolicy::DumpLength; id += 1) {
        if (this->smuMessageCounters[id] == 0) { continue; }
        used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used, "%zu:%u,",
            id, this->smuMessageCounters[id]));
    }
    used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used, "\n"));

    const size_t perEntry = 112;
    const size_t available = used < GfxDiagPolicy::DumpLength ? GfxDiagPolicy::DumpLength - used : 0;
    const size_t maxEntries = available / perEntry;
    const size_t skip = count > maxEntries ? count - maxEntries : 0;
    for (size_t index = skip; index < count && used < GfxDiagPolicy::DumpLength; index += 1) {
        const auto &event = this->ring[(start + index) % GfxDiagPolicy::RingCapacity];
        if (event.text[0] != '\0') {
            used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used,
                "seq=%llu t=%llus tag=%u a=%u text=%s\n", event.seq, event.uptimeSeconds, event.tag, event.a,
                event.text));
        } else {
            used += static_cast<size_t>(snprintf(this->dumpBuffer + used, GfxDiagPolicy::DumpLength - used,
                "seq=%llu t=%llus tag=%u a=%u b=%u c=%u r=%d\n", event.seq, event.uptimeSeconds, event.tag,
                event.a, event.b, event.c, static_cast<int>(event.result)));
        }
    }

    auto *dump = OSString::withCString(this->dumpBuffer);
    if (dump == nullptr) { return; }

    this->provider->setProperty("NootRX_GfxDiagDump", dump);
    dump->release();
    this->dumpSeq += 1;
    this->provider->setProperty("NootRX_GfxDiagDumpSeq", this->dumpSeq, 64);
}

void GfxDiag::buildSummary(char *out, size_t length) {
    if (out == nullptr || length == 0) { return; }
    out[0] = '\0';
    if (!this->enabled || this->lock == nullptr) { return; }

    size_t used = static_cast<size_t>(snprintf(out, length,
        "events=%u dropped=%u flushes=%u probeMask=0x%X", this->storedCount, this->droppedCount, this->flushCount,
        this->probeMask));

    const auto count = this->storedCount;
    const auto start = count == GfxDiagPolicy::RingCapacity ? this->writeIndex : 0;
    const size_t maxEntries = 12;
    const size_t first = count > maxEntries ? count - maxEntries : 0;
    for (size_t index = first; index < count && used + 64 < length; index += 1) {
        const auto &event = this->ring[(start + index) % GfxDiagPolicy::RingCapacity];
        if (event.text[0] != '\0') {
            used += static_cast<size_t>(
                snprintf(out + used, length - used, " | %u:%s", event.tag, event.text));
        } else {
            used += static_cast<size_t>(snprintf(out + used, length - used, " | %u:%u,%u,%u,%d", event.tag,
                event.a, event.b, event.c, static_cast<int>(event.result)));
        }
    }
}

void GfxDiag::requestDump() {
    if (!this->enabled || this->dumpCall == nullptr) { return; }
    thread_call_enter(this->dumpCall);
}

void GfxDiag::dumpCallback(thread_call_param_t param0, thread_call_param_t) {
    auto *diag = static_cast<GfxDiag *>(param0);
    if (diag == nullptr || !diag->enabled) { return; }
    diag->publishDump("requested");
}

void GfxDiag::stallCheckCallback(thread_call_param_t param0, thread_call_param_t) {
    auto *diag = static_cast<GfxDiag *>(param0);
    if (diag == nullptr || !diag->enabled) { return; }

    const auto now = uptimeSeconds();
    UInt64 busySeconds[ChannelSlots] = {};
    UInt32 outstanding[ChannelSlots] = {};
    UInt32 workCount = 0;

    const auto interruptState = IOSimpleLockLockDisableInterrupt(diag->lock);
    for (UInt32 index = 0; index < ChannelSlots; index += 1) {
        outstanding[index] = static_cast<UInt32>(diag->channelOutstanding[index]);
        if (outstanding[index] > 0 && diag->channelPendingStart[index] != 0 &&
            now > diag->channelPendingStart[index]) {
            busySeconds[index] = now - diag->channelPendingStart[index];
        }
    }
    workCount = diag->workSubmittedCount - diag->workSubmittedReported;
    diag->workSubmittedReported = diag->workSubmittedCount;
    IOSimpleLockUnlockEnableInterrupt(diag->lock, interruptState);

    for (UInt32 index = 0; index < ChannelSlots; index += 1) {
        if (outstanding[index] == 0 || busySeconds[index] < 3) { continue; }
        diag->record(GfxDiagTag::QueueEvent, index, static_cast<UInt32>(busySeconds[index]), 3);
    }
    if (workCount > 0) { diag->record(GfxDiagTag::QueueEvent, 0xFFFFU, workCount, 2); }

    if (diag->checkCall != nullptr) {
        thread_call_enter_delayed(diag->checkCall, mach_absolute_time() + secondsToAbsolute(1));
    }
}

void GfxDiag::flush(const char *reason, bool force) {
    if (this->lock == nullptr || this->storedCount == 0) { return; }

    const auto now = uptimeSeconds();
    if (!force && this->lastFlushSeconds != 0 &&
        now < this->lastFlushSeconds + GfxDiagPolicy::FlushCooldownSeconds) {
        return;
    }
    this->lastFlushSeconds = now;

    const auto interruptState = IOSimpleLockLockDisableInterrupt(this->lock);
    const auto count = this->storedCount;
    const auto start = count == GfxDiagPolicy::RingCapacity ? this->writeIndex : 0;
    IOSimpleLockUnlockEnableInterrupt(this->lock, interruptState);

    this->flushCount += 1;
    kprintf("NootRX_GFX flush reason=%s count=%u dropped=%u flush=%u uptime=%llus\n",
        reason != nullptr ? reason : "(none)", count, this->droppedCount, this->flushCount, now);

    for (UInt32 index = 0; index < count; index += 1) {
        const auto slot = (start + index) % static_cast<UInt32>(GfxDiagPolicy::RingCapacity);
        logEvent(this->ring[slot]);
    }

    if (this->provider != nullptr) {
        this->provider->setProperty("NootRX_GfxDiagEvents", this->storedCount, 32);
        this->provider->setProperty("NootRX_GfxDiagDropped", this->droppedCount, 32);
        this->provider->setProperty("NootRX_GfxDiagFlushes", this->flushCount, 32);
    }

    this->publishDump(reason);
}

void GfxDiag::logEvent(const GfxDiagEvent &event) {
    if (event.text[0] != '\0') {
        kprintf("NootRX_GFX seq=%llu t=%llus tag=%u a=%u text=%s\n", event.seq, event.uptimeSeconds, event.tag,
            event.a, event.text);
        return;
    }
    kprintf("NootRX_GFX seq=%llu t=%llus tag=%u a=%u b=%u c=%u result=%d\n", event.seq, event.uptimeSeconds,
        event.tag, event.a, event.b, event.c, static_cast<int>(event.result));
}

void GfxDiag::heartbeatCallback(thread_call_param_t param0, thread_call_param_t) {
    auto *diag = static_cast<GfxDiag *>(param0);
    if (diag == nullptr || !diag->enabled) { return; }

    diag->record(GfxDiagTag::Heartbeat, diag->storedCount, diag->droppedCount, diag->flushCount);
    diag->heartbeatCount += 1;
    SYSLOG("GfxDiag", "heartbeat: events=%u dropped=%u flushes=%u uptime=%llus", diag->storedCount,
        diag->droppedCount, diag->flushCount, uptimeSeconds());

    if (diag->provider != nullptr) {
        diag->provider->setProperty("NootRX_GfxDiagEvents", diag->storedCount, 32);
        diag->provider->setProperty("NootRX_GfxDiagDropped", diag->droppedCount, 32);
    }

    diag->publishDump("heartbeat");

    if (diag->heartbeatCall != nullptr) {
        thread_call_enter_delayed(
            diag->heartbeatCall, mach_absolute_time() + secondsToAbsolute(GfxDiagPolicy::HeartbeatSeconds));
    }
}

UInt64 GfxDiag::uptimeSeconds() {
    UInt64 nanoseconds = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &nanoseconds);
    return nanoseconds / 1000000000ULL;
}

#endif    // NOOTRX_GFX_DIAG
