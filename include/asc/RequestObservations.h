#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace asc {
struct RequestObservation {
    int id=0,flags=0;
    uint32_t requestedAt=0;
    bool onMainThread=false;
};
enum class ObservationResult { Recorded, Coalesced, Ignored };

// Per-model mailboxes replace an event queue: repeated requests cannot crowd
// out other IDs or promotions. Main/worker lanes keep provenance independent.
// Reset requires quiescent producers. Drain has a single game-thread consumer.
template<std::size_t ModelCount>
class RequestObservations {
    static constexpr std::size_t Entries=ModelCount*2;
    static constexpr std::size_t Words=(Entries+31)/32;
    struct Entry {
        // High 32 bits: earliest pending timestamp; low byte: flags;
        // bit 8: presence (including timestamp zero with no flags).
        std::atomic<uint64_t> value{0};
    };
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static_assert(ModelCount>0);
public:
    ObservationResult Observe(int id,int flags,uint32_t now,bool mainThread) {
        if(id<0||std::size_t(id)>=ModelCount)return ObservationResult::Ignored;
        const std::size_t index=std::size_t(id)*2+(mainThread?1:0);
        auto& entry=entries_[index].value;
        uint64_t old=entry.load(std::memory_order_relaxed);
        // GTA RequestModel flags are an unsigned byte. Preserve all 8 bits.
        const uint64_t bits=uint8_t(flags)|uint64_t(0x100);
        for(;;){
            const uint64_t next=old?(old|bits):((uint64_t(now)<<32)|bits);
            if(next==old)return ObservationResult::Coalesced;
            if(entry.compare_exchange_weak(old,next,std::memory_order_relaxed)){
                if(old)return ObservationResult::Coalesced;
                dirty_[index/32].fetch_or(uint32_t(1)<<(index%32),
                                          std::memory_order_release);
                return ObservationResult::Recorded;
            }
        }
    }
    template<class Callback>
    unsigned Drain(Callback&& callback,unsigned limit=1024){
        unsigned count=0;
        if(!limit)return count;
        // A rotating word cursor prevents busy low IDs from starving high IDs.
        for(std::size_t visited=0;visited<Words;++visited){
            const std::size_t word=cursor_;
            cursor_=(cursor_+1)%Words;
            if(!dirty_[word].load(std::memory_order_acquire))continue;
            uint32_t bits=dirty_[word].exchange(0,std::memory_order_acquire);
            while(bits){
                const unsigned bit=std::countr_zero(bits);
                bits&=bits-1;
                const std::size_t index=word*32+bit;
                const uint64_t value=entries_[index].value.exchange(0,
                    std::memory_order_relaxed);
                if(value){
                    callback(RequestObservation{int(index/2),int(value&0xFF),
                        uint32_t(value>>32),(index&1)!=0});
                    ++count;
                }
                if(count==limit){
                    if(bits)dirty_[word].fetch_or(bits,std::memory_order_release);
                    return count;
                }
            }
        }
        return count;
    }
    void Reset(){
        for(auto& entry:entries_)entry.value.store(0,std::memory_order_relaxed);
        for(auto& word:dirty_)word.store(0,std::memory_order_relaxed);
        cursor_=0;
    }
private:
    std::array<Entry,Entries> entries_{};
    std::array<std::atomic<uint32_t>,Words> dirty_{};
    std::size_t cursor_=0;
};
}
