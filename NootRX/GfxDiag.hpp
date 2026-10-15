// Copyright © 2023-2026 ChefKiss. Licensed under the Thou Shalt Not Profit License version 1.5.
// See LICENSE for details.

#pragma once

#include "DiagPolicy.hpp"
#include <Headers/kern_util.hpp>
#include <IOKit/IOLocks.h>
#include <kern/thread_call.h>

class IOPCIDevice;

// Event categories recorded by the bounded GFX/KIQ diagnostics.
enum class GfxDiagTag : UInt32 {
    Boot = 0,
    PspSubmit,
    SmuMessage,
    Firmware,
    GcEvent,
    LoggerMessage,
    LoggerFailure,
    GpuPanic,
    Heartbeat,
    ResetEvent,
    QueueEvent,
};

// One fixed-size ring slot. Text is truncated and never contains kernel or GPU addresses.
struct GfxDiagEvent {
    UInt64 seq;
    UInt64 uptimeSeconds;
    UInt32 tag;
    UInt32 a;
    UInt32 b;
    UInt32 c;
    SInt32 result;
    char text[GfxDiagPolicy::TextLength];
};

#if !NOOTRX_GFX_DIAG

// Release builds get an empty recorder: every call site stays source compatible and
// compiles to nothing.
class GfxDiag {
    public:
    void configure(IOPCIDevice *, bool) {}
    bool isEnabled() const { return false; }
    void record(GfxDiagTag, UInt32, UInt32 = 0, UInt32 = 0, SInt32 = 0) {}
    void recordMessage(GfxDiagTag, const char *, UInt32 = 0) {}
    void countMessageType(UInt32) {}
    UInt32 countSmuMessage(UInt32) { return 0; }
    void publishProbeMask(UInt32) {}
    void publishDump(const char *) {}
    void requestDump() {}
    void buildSummary(char *out, size_t length) {
        if (out != nullptr && length > 0) { out[0] = '\0'; }
    }
    void disable(const char *) {}
    void flush(const char *, bool = false) {}
    void channelSubmit(UInt64) {}
    void channelComplete(UInt64) {}
    void noteWorkSubmitted() {}
};

#else

class GfxDiag {
    public:
    // Prepares the recorder for the detected GPU and starts the heartbeat when enabled.
    void configure(IOPCIDevice *provider, bool enabled);

    // Returns whether event recording is active.
    bool isEnabled() const { return this->enabled; }

    // Records one scalar event; a cheap no-op while the recorder is disabled.
    void record(GfxDiagTag tag, UInt32 a, UInt32 b = 0, UInt32 c = 0, SInt32 result = 0);

    // Records one driver message with its text truncated to the ring slot size.
    void recordMessage(GfxDiagTag tag, const char *message, UInt32 a = 0);

    // Counts one driver message type even when the message itself is not kept.
    void countMessageType(UInt32 logType);

    // Counts one SMU message id and returns the count observed before this call.
    UInt32 countSmuMessage(UInt32 msgId);

    // Publishes the GC probe route mask as its own property so it survives ring churn.
    void publishProbeMask(UInt32 mask);

    // Records per-channel pending submissions for the one-second stall checker.
    void channelSubmit(UInt64 channel);
    void channelComplete(UInt64 channel);

    // Records that the driver signalled newly submitted GPU work to power management.
    void noteWorkSubmitted();

    // Publishes the current ring as one bounded IORegistry string for a user-space collector.
    void publishDump(const char *reason);

    // Requests an asynchronous dump from a driver callback context that must not touch IORegistry.
    void requestDump();

    // Formats counters plus the newest events into one line for the panic payload.
    void buildSummary(char *out, size_t length);

    // Reports that one diagnostic route failed; disables the recorder without panicking.
    void disable(const char *reason);

    // Writes the ring plus counters to the kernel log; rate limited unless forced.
    void flush(const char *reason, bool force = false);

    private:
    // Emits one ring slot as a compact kernel log line.
    static void logEvent(const GfxDiagEvent &event);

    // Heartbeat callback that emits counters and re-arms itself.
    static void heartbeatCallback(thread_call_param_t param0, thread_call_param_t param1);

    // Asynchronous dump callback used by driver-context probes.
    static void dumpCallback(thread_call_param_t param0, thread_call_param_t param1);

    // One-second stall checker: reports channels that stay busy and work-signal rates.
    static void stallCheckCallback(thread_call_param_t param0, thread_call_param_t param1);

    // Returns the seconds since boot for event timestamps.
    static UInt64 uptimeSeconds();

    // Returns the stable index of one channel object, allocating a slot on first sight.
    UInt32 channelIndexLocked(UInt64 channel);

    IOSimpleLock *lock {nullptr};
    IOPCIDevice *provider {nullptr};
    GfxDiagEvent ring[GfxDiagPolicy::RingCapacity] {};
    UInt64 nextSeq {1};
    UInt32 writeIndex {0};
    UInt32 storedCount {0};
    UInt32 droppedCount {0};
    UInt32 flushCount {0};
    UInt32 heartbeatCount {0};
    UInt32 messageTypeCounters[GfxDiagPolicy::MaxLogTypes] {};
    UInt32 smuMessageCounters[GfxDiagPolicy::MaxSmuMessages] {};
    static constexpr size_t ChannelSlots = 16;
    UInt64 channelKeys[ChannelSlots] {};
    UInt32 channelPending[ChannelSlots] {};
    UInt32 channelSubmits[ChannelSlots] {};
    UInt64 channelBusyStart[ChannelSlots] {};
    UInt32 workSubmittedCount {0};
    UInt32 workSubmittedReported {0};
    UInt32 probeMask {0};
    UInt64 dumpSeq {0};
    UInt64 lastFlushSeconds {0};
    bool enabled {false};
    thread_call_t heartbeatCall {nullptr};
    thread_call_t dumpCall {nullptr};
    thread_call_t checkCall {nullptr};
    char dumpBuffer[GfxDiagPolicy::DumpLength] {};
};

#endif
