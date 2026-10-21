#include "asc/ArchivePreload.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <deque>
#include <fstream>
#include <limits>
#include <new>
#include <system_error>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace asc::archive_preload {
namespace {
constexpr uint64_t MiB=1048576ull, Sector=2048ull;
struct Job { std::filesystem::path path; std::string name; uint64_t offset=0,size=0; bool binary=false; };
struct Item { std::shared_ptr<const std::vector<uint8_t>> data; uint64_t size=0; };
struct State {
    Settings settings;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::condition_variable wake;
    std::unordered_map<std::string,Job> indexed;
    std::deque<std::string> pending;
    std::unordered_set<std::string> enqueued;
    std::unordered_map<std::string,Item> cache;
    std::deque<std::string> oldest; // FIFO eviction; no game-thread disk I/O
    Stats stats;
    uint32_t lastReport=0;
};
State& S() {static State s;return s;}
std::string Lower(std::string name) {
    for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
bool Candidate(const std::string& name,const Settings& cfg) {
    const auto low=Lower(name);
    const auto dot=low.rfind('.');
    if(dot==std::string::npos)return false;
    const auto ext=low.substr(dot);
    if(ext==".ipl")return cfg.binaryIpls;
    if(!cfg.models)return false;
    return ext==".dff"||ext==".txd"||ext==".col"||ext==".ifp";
}
uint32_t ReadU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1])<<8) |
        (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
}
uint16_t ReadU16(const uint8_t* p) {
    return uint16_t(uint16_t(p[0]) | (uint16_t(p[1])<<8));
}
void Reject() {
    auto& s=S();
    std::lock_guard guard(s.mutex);
    ++s.stats.rejected;
}
void IndexImg(const std::filesystem::path& path,std::vector<Job>& jobs,
              const Settings& cfg) {
    std::error_code ec;
    const uint64_t length=std::filesystem::file_size(path,ec);
    if(ec||length<8)return;
    std::ifstream input(path,std::ios::binary);
    uint8_t header[8]{};
    if(!input.read(reinterpret_cast<char*>(header),sizeof(header)))return;
    if(std::memcmp(header,"VER2",4)!=0)return; // No parsing old DIR/IMG pairs.
    const uint32_t count=ReadU32(header+4);
    if(!count||count>150000 || 8ull+uint64_t(count)*32ull>length){Reject();return;}
    for(uint32_t i=0;i<count && jobs.size()<50000 && !S().stop.load();++i){
        uint8_t entry[32]{};
        if(!input.read(reinterpret_cast<char*>(entry),sizeof(entry)))break;
        // Header schema: sector offset(u32), streaming sectors(u16),
        // actual sectors(u16), null-terminated or fixed ASCII name[24].
        const uint64_t begin=uint64_t(ReadU32(entry))*Sector;
        const uint16_t sectors=ReadU16(entry+6)?ReadU16(entry+6):ReadU16(entry+4);
        const uint64_t size=uint64_t(sectors)*Sector;
        const char* b=reinterpret_cast<const char*>(entry+8);
        const auto nul=static_cast<const char*>(std::memchr(b,0,24));
        const char* end=nul?nul:(b+24);
        while(end>b && end[-1]==' ')--end;
        if(end==b){Reject();continue;}
        std::string name(b,end);
        if(name.find_first_of("/\\:")!=std::string::npos ||
           !Candidate(name,cfg))continue;
        if(!size||size>uint64_t(cfg.maxEntryMiB)*MiB||
           begin>length || size>length-begin){Reject();continue;}
        jobs.push_back({path,Lower(name),begin,size,
                        Lower(name).ends_with(".ipl")});
    }
}
void IndexLoose(const std::filesystem::path& root,std::vector<Job>& jobs,
                const Settings& cfg) {
    // Only within the GTA installation; recursive iterator refuses symlinks.
    const auto folder=root/"data"/"maps";
    std::error_code ec;
    if(!std::filesystem::exists(folder,ec))return;
    std::filesystem::recursive_directory_iterator iter(folder,
        std::filesystem::directory_options::skip_permission_denied,ec);
    const std::filesystem::recursive_directory_iterator end;
    unsigned visited=0;
    for(;iter!=end && !S().stop.load() && visited<20000;iter.increment(ec)){
        if(ec){ec.clear();continue;}
        ++visited;
        if(!iter->is_regular_file(ec) || ec) {ec.clear();continue;}
        const auto path=iter->path();
        const auto name=Lower(path.filename().string());
        if(!Candidate(name,cfg))continue;
        const uint64_t size=iter->file_size(ec);
        if(ec||!size||size>uint64_t(cfg.maxEntryMiB)*MiB){
            ec.clear();continue;
        }
        jobs.push_back({path,name,0,size,name.ends_with(".ipl")});
        if(jobs.size()>=50000)break;
    }
}
void Store(Job job,std::vector<uint8_t>&& bytes) {
    auto& s=S();
    std::lock_guard guard(s.mutex);
    if(s.stop.load())return;
    const uint64_t cap=uint64_t(s.settings.maxCacheMiB)*MiB;
    if(bytes.size()>cap)return;
    // Keep the cache immutable to consumers; no shared-pointer invalidation.
    if(s.cache.find(job.name)!=s.cache.end())return;
    while(!s.oldest.empty() && s.stats.bytes+bytes.size()>cap){
        const auto oldest=s.oldest.front();s.oldest.pop_front();
        const auto it=s.cache.find(oldest);
        if(it==s.cache.end())continue;
        s.stats.bytes-=it->second.size;
        s.cache.erase(it);
        ++s.stats.evicted;
    }
    auto data=std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    const uint64_t size=data->size();
    // Keep eviction order and byte accounting consistent if allocation fails.
    s.oldest.push_back(job.name);
    try {s.cache.emplace(job.name,Item{std::move(data),size});}
    catch(...){s.oldest.pop_back();throw;}
    s.stats.bytes+=size;
    ++s.stats.loaded;
}
void Run(std::filesystem::path root) {
    auto& s=S();
    auto cfg=s.settings;
    std::vector<Job> jobs;
    // Prefer loose override files, then archives. First match wins.
    IndexLoose(root,jobs,cfg);
    std::error_code ec;
    const auto models=root/"models";
    if(std::filesystem::exists(models,ec)) {
        for(const auto& entry:std::filesystem::directory_iterator(models,
              std::filesystem::directory_options::skip_permission_denied,ec)){
            if(s.stop.load()||jobs.size()>=50000)break;
            if(!entry.is_regular_file(ec)){ec.clear();continue;}
            if(Lower(entry.path().extension().string())==".img")
                IndexImg(entry.path(),jobs,cfg);
        }
    }
    {
        std::lock_guard guard(s.mutex);
        for(auto& job:jobs) {
            auto name=job.name;
            if(s.indexed.emplace(name,std::move(job)).second) {
                s.pending.push_back(name);
                s.enqueued.insert(name);
            }
        }
        s.stats.indexed=s.indexed.size();
    }
    // The worker remains alive after initial staging. Later TryGet() misses
    // requeue the original source bytes without any disk work on GTA's thread.
    while(!s.stop.load()){
        Job job{};
        std::string name;
        {
            std::unique_lock guard(s.mutex);
            s.wake.wait(guard,[&]{return s.stop.load()||!s.pending.empty();});
            if(s.stop.load())break;
            name=std::move(s.pending.front());
            s.pending.pop_front();
            auto it=s.indexed.find(name);
            if(it==s.indexed.end()||s.cache.contains(name)){
                s.enqueued.erase(name);continue;
            }
            job=it->second;
        }
        struct Completion {
            State& state;const std::string& name;
            ~Completion(){std::lock_guard guard(state.mutex);state.enqueued.erase(name);}
        } complete{s,name};
        std::ifstream input(job.path,std::ios::binary);
        if(!input || job.offset>uint64_t(std::numeric_limits<std::streamoff>::max())){
            Reject();continue;
        }
        input.seekg(static_cast<std::streamoff>(job.offset));
        std::vector<uint8_t> bytes;
        try {bytes.resize(static_cast<size_t>(job.size));}
        catch(const std::bad_alloc&){Reject();continue;}
        if(!input.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()))){Reject();continue;}
        if(job.binary && (bytes.size()<32 ||
                          std::memcmp(bytes.data(),"bnry",4)!=0)){
            Reject();continue;
        }
        try {Store(std::move(job),std::move(bytes));}
        catch(const std::bad_alloc&){Reject();continue;}
        std::this_thread::sleep_for(
            std::chrono::milliseconds(cfg.sleepBetweenReadsMs));
    }
}
}
void Start(const std::filesystem::path& root,Settings settings) {
    Stop();
    auto& s=S();
    s.settings=settings;
    if(!settings.enabled||(!settings.models&&!settings.binaryIpls))return;
    s.stop=false;
    try{
        s.worker=std::thread([root]{
            try {Run(root);}
            catch(...) {
                S().stop.store(true);
                Reject();
            } // A failed worker must not keep accepting jobs it cannot service.
        });
    } catch(const std::system_error&){s.stop=true;}
}
std::shared_ptr<const std::vector<uint8_t>> TryGet(const std::string& name){
    auto& s=S();
    std::unique_lock guard(s.mutex,std::try_to_lock);
    if(!guard.owns_lock())return nullptr;
    try {
        const std::string key=Lower(name);
        const auto it=s.cache.find(key);
        if(it!=s.cache.end())return it->second.data;
        // Immutable hits remain usable after a worker failure, but misses
        // must not enqueue jobs for a worker that has already exited.
        if(s.stop.load())return nullptr;
        // Includes in-flight reads, not only jobs still waiting in the queue.
        if(s.indexed.contains(key) && s.enqueued.size()<50000 &&
           s.enqueued.insert(key).second){
            try {s.pending.push_back(key);}
            catch(...){s.enqueued.erase(key);throw;}
            s.wake.notify_one();
        }
    }catch(const std::bad_alloc&){++s.stats.rejected;}
    return nullptr;
}
Stats Snapshot(){
    auto& s=S();std::lock_guard guard(s.mutex);return s.stats;
}
void Report(uint32_t now,std::ostream& log){
    auto& s=S();
    if(!s.settings.enabled||uint32_t(now-s.lastReport)<s.settings.reportMs)return;
    Stats stats{};
    {
        std::unique_lock guard(s.mutex,std::try_to_lock);
        if(!guard.owns_lock())return;
        stats=s.stats;
    }
    s.lastReport=now;
    log<<"Archive cache: indexed="<<stats.indexed
       <<", loaded="<<stats.loaded<<", cached MiB="<<stats.bytes/MiB
       <<", evicted="<<stats.evicted<<", rejected="<<stats.rejected<<'\n';
}
void Stop(){
    auto& s=S();
    s.stop=true;
    s.wake.notify_all();
    if(s.worker.joinable())s.worker.join();
    std::lock_guard guard(s.mutex);
    s.cache.clear();s.oldest.clear();s.indexed.clear();
    s.pending.clear();s.enqueued.clear();s.stats={};
}
}
