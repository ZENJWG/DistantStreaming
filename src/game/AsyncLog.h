#pragma once
#include "asc/BoundedRing.h"
#include <windows.h>
#include <array>
#include <atomic>
#include <ostream>
#include <streambuf>
#include <string>

// Single game-thread ostream producer; file I/O belongs to the worker.
// Full queues drop diagnostic chunks instead of blocking gameplay or growing.
class AsyncLog final : public std::ostream,private std::streambuf {
public:
    AsyncLog();
    bool Open(const std::string& path);
    // Only at game shutdown, after all producers have stopped; never DllMain.
    void Stop();
private:
    using int_type=std::streambuf::int_type;
    using traits_type=std::streambuf::traits_type;
    static constexpr size_t ChunkSize=2048;
    struct Chunk {DWORD size=0;std::array<char,ChunkSize> bytes{};};
    int sync() override;
    int_type overflow(int_type value) override;
    void Publish();
    static DWORD WINAPI Entry(void* self);
    void Work();
    std::array<char,ChunkSize> buffer_{};
    asc::BoundedRing<Chunk,64> queue_;
    std::atomic<uint64_t> dropped_{0};
    std::atomic<bool> stopping_{false};
    HANDLE event_=nullptr,thread_=nullptr,file_=INVALID_HANDLE_VALUE;
};
