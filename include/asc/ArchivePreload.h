#pragma once
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>
namespace asc::archive_preload {
struct Settings {
    bool enabled=false;
    bool binaryIpls=false;
    bool models=false;
    uint32_t maxCacheMiB=192;
    uint32_t maxEntryMiB=8;
    uint32_t sleepBetweenReadsMs=5;
    uint32_t reportMs=10000;
};
struct Stats {
    uint64_t indexed=0, loaded=0, rejected=0, evicted=0, bytes=0;
};
// Bounded, validated, read-only source-byte cache. Never invokes GTA IPL
// parsing or alters GTA's archive reader. Caller receives independent const
// buffer ownership, safe even after cache eviction or shutdown.
void Start(const std::filesystem::path& gameRoot, Settings settings);
std::shared_ptr<const std::vector<uint8_t>> TryGet(const std::string& entryName);
Stats Snapshot();
void Report(uint32_t now,std::ostream& log);
void Stop();
}
