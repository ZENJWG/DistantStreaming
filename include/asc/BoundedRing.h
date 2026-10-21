#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace asc {
// Bounded non-blocking multi-producer / multi-consumer ring (Vyukov).
// T must be trivially copyable. No allocations or mutex waits during Push/Pop.
// Reset is only legal when no producers or consumers are running.
template<class T, std::size_t Capacity>
class BoundedRing {
    static_assert(Capacity>=2 && (Capacity&(Capacity-1))==0,
                  "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable<T>::value,
                  "Ring records must be trivially copyable");
    struct Slot {
        std::atomic<std::size_t> sequence{0};
        T item{};
    };
public:
    BoundedRing(){Reset();}
    BoundedRing(const BoundedRing&)=delete;
    BoundedRing&operator=(const BoundedRing&)=delete;

    void Reset(){
        push_.store(0,std::memory_order_relaxed);
        pop_.store(0,std::memory_order_relaxed);
        for(std::size_t i=0;i<Capacity;++i)
            slots_[i].sequence.store(i,std::memory_order_relaxed);
    }
    bool Push(const T&item){
        std::size_t pos=push_.load(std::memory_order_relaxed);
        for(;;) {
            Slot&slot=slots_[pos&(Capacity-1)];
            auto seq=slot.sequence.load(std::memory_order_acquire);
            auto delta=static_cast<std::intptr_t>(seq-pos);
            if(delta==0){
                if(push_.compare_exchange_weak(pos,pos+1,
                        std::memory_order_relaxed)){
                    slot.item=item;
                    slot.sequence.store(pos+1,std::memory_order_release);
                    return true;
                }
            }else if(delta<0)return false;
            else pos=push_.load(std::memory_order_relaxed);
        }
    }
    bool Pop(T&item){
        std::size_t pos=pop_.load(std::memory_order_relaxed);
        for(;;) {
            Slot&slot=slots_[pos&(Capacity-1)];
            auto seq=slot.sequence.load(std::memory_order_acquire);
            auto delta=static_cast<std::intptr_t>(seq-(pos+1));
            if(delta==0){
                if(pop_.compare_exchange_weak(pos,pos+1,
                        std::memory_order_relaxed)){
                    item=slot.item;
                    slot.sequence.store(pos+Capacity,std::memory_order_release);
                    return true;
                }
            }else if(delta<0)return false;
            else pos=pop_.load(std::memory_order_relaxed);
        }
    }
private:
    std::array<Slot,Capacity> slots_{};
    alignas(64) std::atomic<std::size_t> push_{0};
    alignas(64) std::atomic<std::size_t> pop_{0};
};
}
