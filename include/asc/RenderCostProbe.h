#pragma once
// Portable CPU-side diagnostics only. No GPU queries, resource copies, frame
// pacing changes or model/renderer policy changes.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace asc::render_cost {
enum class Stage : unsigned {
    GpuFrame,
    MemoryReassert,
    TelemetryFrame,
    ControllerTick,
    StateCapture,
    Cleanup,
    Count
};
inline constexpr std::array<const char*,static_cast<unsigned>(Stage::Count)>
    StageNames{"gpuFrame","memoryReassert","telemetryFrame","controllerTick",
               "stateCapture","cleanup"};
struct Timing {
    uint64_t calls=0, sumUs=0, peakUs=0;
    void Add(uint64_t us) noexcept {
        ++calls;
        sumUs=us>UINT64_MAX-sumUs?UINT64_MAX:sumUs+us;
        peakUs=std::max(peakUs,us);
    }
    uint64_t Average() const noexcept {return calls?sumUs/calls:0;}
};
struct StageSnapshot {
    std::array<Timing,static_cast<unsigned>(Stage::Count)> stages{};
};
class StageProbe {
    StageSnapshot window_{};
public:
    void Add(Stage stage,uint64_t us) noexcept {
        const auto index=static_cast<unsigned>(stage);
        if(index<window_.stages.size())window_.stages[index].Add(us);
    }
    StageSnapshot TakeWindow() noexcept {
        const auto result=window_;
        window_={};
        return result;
    }
};

enum class DrawKind : unsigned {
    Primitive,
    IndexedPrimitive,
    PrimitiveUP,
    IndexedPrimitiveUP,
    Count
};
inline constexpr std::array<const char*,static_cast<unsigned>(DrawKind::Count)>
    DrawNames{"DrawPrimitive","DrawIndexedPrimitive","DrawPrimitiveUP","DrawIndexedPrimitiveUP"};
inline constexpr uint32_t DrawSampleRate=128;
struct DrawSample {
    uint64_t calls=0, samples=0, totalTicks=0, peakTicks=0, failures=0;
    uint64_t slow1ms=0, slow5ms=0, slow15ms=0;
};
struct DrawSnapshot {
    std::array<DrawSample,static_cast<unsigned>(DrawKind::Count)> kinds{};
};
class DrawProbe {
    struct AtomicKind {
        std::atomic<uint64_t> calls{0}, samples{0}, ticks{0}, peak{0}, failures{0};
        std::atomic<uint64_t> slow1ms{0}, slow5ms{0}, slow15ms{0};
    };
    std::array<AtomicKind,static_cast<unsigned>(DrawKind::Count)> counters_{};
public:
    // Called exactly once per draw. The caller measures and calls RecordSample
    // ONLY when this returns true. This keeps QPC/mutex work off 127/128 draws.
    bool Begin(DrawKind kind) noexcept {
        const auto i=static_cast<unsigned>(kind);
        if(i>=counters_.size())return false;
        const uint64_t serial=counters_[i].calls.fetch_add(
            1,std::memory_order_relaxed)+1;
        return (serial&(DrawSampleRate-1u))==0u;
    }
    void RecordSample(DrawKind kind,uint64_t elapsedTicks,bool success=true,
                      uint64_t clockFrequency=0) noexcept {
        const auto i=static_cast<unsigned>(kind);
        if(i>=counters_.size())return;
        auto& c=counters_[i];
        c.samples.fetch_add(1,std::memory_order_relaxed);
        c.ticks.fetch_add(elapsedTicks,std::memory_order_relaxed);
        if(!success)c.failures.fetch_add(1,std::memory_order_relaxed);
        if(clockFrequency>=1000) {
            const uint64_t ticksPerMs=clockFrequency/1000;
            if(elapsedTicks>=ticksPerMs)
                c.slow1ms.fetch_add(1,std::memory_order_relaxed);
            if(elapsedTicks>=ticksPerMs*5)
                c.slow5ms.fetch_add(1,std::memory_order_relaxed);
            if(elapsedTicks>=ticksPerMs*15)
                c.slow15ms.fetch_add(1,std::memory_order_relaxed);
        }
        uint64_t previous=c.peak.load(std::memory_order_relaxed);
        while(previous<elapsedTicks &&
              !c.peak.compare_exchange_weak(previous,elapsedTicks,
                  std::memory_order_relaxed,std::memory_order_relaxed)) {}
    }
    DrawSnapshot TakeWindow() noexcept {
        DrawSnapshot result{};
        for(unsigned i=0;i<counters_.size();++i) {
            auto& c=counters_[i];
            auto& o=result.kinds[i];
            // A call concurrent with snapshotting may land in adjacent windows.
            // Diagnostic windows are approximate; no waiting in the draw thread.
            o.calls=c.calls.exchange(0,std::memory_order_relaxed);
            o.samples=c.samples.exchange(0,std::memory_order_relaxed);
            o.totalTicks=c.ticks.exchange(0,std::memory_order_relaxed);
            o.peakTicks=c.peak.exchange(0,std::memory_order_relaxed);
            o.failures=c.failures.exchange(0,std::memory_order_relaxed);
            o.slow1ms=c.slow1ms.exchange(0,std::memory_order_relaxed);
            o.slow5ms=c.slow5ms.exchange(0,std::memory_order_relaxed);
            o.slow15ms=c.slow15ms.exchange(0,std::memory_order_relaxed);
        }
        return result;
    }
};
// Use split division to avoid overflowing uint64_t at large tick counts.
inline uint64_t TicksToUs(uint64_t ticks,uint64_t frequency) noexcept {
    if(!frequency)return 0;
    const uint64_t seconds=ticks/frequency;
    if(seconds>UINT64_MAX/1000000ull)return UINT64_MAX;
    const uint64_t remaining=ticks%frequency;
    return seconds*1000000ull+(remaining*1000ull/frequency)*1000ull+
        ((remaining*1000ull%frequency)*1000ull)/frequency;
}
} // namespace asc::render_cost
