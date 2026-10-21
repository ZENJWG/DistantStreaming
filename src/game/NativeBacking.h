#pragma once
#include "asc/NativeMemoryProtocol.h"
#include <windows.h>
#include <string>
#include <ostream>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <utility>

namespace native_memory {
class Backing;
class Session : public std::enable_shared_from_this<Session> {
public:
    static std::shared_ptr<Session> Start(const std::wstring& exe,unsigned budgetMiB,std::ostream& log);
    ~Session();
    std::unique_ptr<Backing> Allocate(uint64_t bytes);
    void Release(uint64_t id);
    bool Stats(asc::native::Reply& out);
    bool ReadAsset(HANDLE source,uint64_t offset,void* output,DWORD bytes,DWORD& done);
    // Retire a temporary per-read handle after the background request.
    // The helper's pages stay cached by file identity, independent of source IDs.
    void CloseAssetSource(HANDLE source);
    bool AssetStats(asc::native::AssetSummary& out);
    bool CacheCoverage(asc::native::CacheCoverage& out);
    bool CatalogConfigure(bool enabled);
    bool CatalogStatus(asc::native::CatalogSummary& out);
    bool CatalogQueryCell(int32_t x,int32_t y,asc::native::CatalogCellReply& out);
    // Warm sources stay registered until helper shutdown; temporary reads do not.
    bool RegisterWarmSource(HANDLE source);
    bool RegisterVerifiedImgMember(
        HANDLE selectedSource,const asc::native::ObservedImgMemberWarm& observed,
        uint64_t& memberOffset,uint64_t& memberBytes);
    DWORD Pid()const{return pid_;}
    std::pair<const char*,DWORD> LastTransportError();
private:
    bool Exchange(asc::native::Command cmd,uint64_t id,uint64_t bytes,asc::native::Reply& out,
                  const void* payload=nullptr,DWORD payloadBytes=0,void* extra=nullptr,DWORD extraBytes=0);
    uint64_t NextId();
    std::wstring name_;
    HANDLE process_=nullptr;
    HANDLE pipe_=INVALID_HANDLE_VALUE; // One verified duplex connection per session
    DWORD pid_=0;
    DWORD lastConnectError_=0;
    const char* lastConnectStage_="not attempted";
    uint64_t next_=1;
    std::mutex mutex_;
    std::mutex assetMutex_;
    struct Source {uint64_t id;DWORD volume,high,low;};
    std::unordered_map<HANDLE,Source> sources_;
    HANDLE transfer_=nullptr;
    const void* transferView_=nullptr;
    uint64_t transferId_=0;
};
class View {
public:
    View()=default;
    ~View(){Reset();}
    View(const View&)=delete;
    View& operator=(const View&)=delete;
    bool Map(HANDLE mapping,uint64_t offset,size_t bytes);
    void Reset(){if(base_)UnmapViewOfFile(base_);base_=nullptr;data_=nullptr;}
    void* Data()const{return data_;}
private:
    void* base_=nullptr;
    void* data_=nullptr;
};
class Backing {
public:
    Backing(std::shared_ptr<Session> session,uint64_t id,uint64_t bytes,HANDLE mapping):
        session_(std::move(session)),id_(id),bytes_(bytes),mapping_(mapping){}
    ~Backing(){CloseHandle(mapping_);session_->Release(id_);}
    bool Map(View& view,uint64_t offset,size_t bytes){
        return offset<=bytes_&&bytes<=bytes_-offset&&view.Map(mapping_,offset,bytes);
    }
private:
    std::shared_ptr<Session> session_;
    uint64_t id_,bytes_;
    HANDLE mapping_;
};
}
