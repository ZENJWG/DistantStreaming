#include "asc/BoundedRing.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)

struct Record {int producer;int sequence;};
int main(){
    try {
        asc::BoundedRing<Record,8> ring;
        for(int i=0;i<8;++i)CHECK(ring.Push({0,i}));
        CHECK(!ring.Push({0,99}));
        for(int i=0;i<8;++i){
            Record r{};
            CHECK(ring.Pop(r)&&r.producer==0&&r.sequence==i);
        }
        Record empty{};
        CHECK(!ring.Pop(empty));
        ring.Reset();
        constexpr int producers=4, each=10000;
        constexpr int total=producers*each;
        asc::BoundedRing<Record,1024> concurrent;
        std::atomic<bool> ready{false};
        std::array<std::thread,producers> threads;
        for(int t=0;t<producers;++t)
            threads[t]=std::thread([&,t]{
                while(!ready.load(std::memory_order_acquire))
                    std::this_thread::yield();
                for(int i=0;i<each;++i){
                    Record r{t,i};
                    while(!concurrent.Push(r))std::this_thread::yield();
                }
            });
        ready.store(true,std::memory_order_release);
        std::vector<uint8_t> seen(total,0);
        std::array<int,producers> previous{};
        previous.fill(-1);
        int consumed=0;
        while(consumed<total){
            Record r{};
            if(!concurrent.Pop(r)){std::this_thread::yield();continue;}
            CHECK(r.producer>=0&&r.producer<producers);
            CHECK(r.sequence>=0&&r.sequence<each);
            const auto at=r.producer*each+r.sequence;
            CHECK(!seen[at]);
            seen[at]=1;
            CHECK(r.sequence==previous[r.producer]+1);
            previous[r.producer]=r.sequence;
            ++consumed;
        }
        for(auto&thread:threads)thread.join();
        for(auto yes:seen)CHECK(yes==1);
        CHECK(!concurrent.Pop(empty));
        std::cout<<"PASS: MPMC overflow, FIFO, four-producer stress\n";
        return 0;
    }catch(const std::exception&e){
        std::cerr<<"FAIL: "<<e.what()<<'\n';
        return 1;
    }
}
