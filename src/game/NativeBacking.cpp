#include "NativeBacking.h"
#include <bcrypt.h>
#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace native_memory {
namespace {
bool Transfer(HANDLE pipe,void* bytes,DWORD count,bool write,uint64_t deadline){
    auto* data=static_cast<unsigned char*>(bytes);
    while(count){
        if(GetTickCount64()>=deadline){SetLastError(ERROR_TIMEOUT);return false;}
        OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!ov.hEvent)return false;
        DWORD done=0;
        BOOL ok=write?WriteFile(pipe,data,count,&done,&ov):ReadFile(pipe,data,count,&done,&ov);
        if(!ok&&GetLastError()==ERROR_IO_PENDING){
            const auto now=GetTickCount64();
            const DWORD remaining=now<deadline?DWORD(deadline-now):0;
            if(WaitForSingleObject(ov.hEvent,remaining)==WAIT_OBJECT_0)
                ok=GetOverlappedResult(pipe,&ov,&done,FALSE);
            else {
                CancelIoEx(pipe,&ov);
                GetOverlappedResult(pipe,&ov,&done,TRUE);
                ok=FALSE;
                SetLastError(ERROR_TIMEOUT);
            }
        }
        const DWORD error=ok?(done?ERROR_SUCCESS:ERROR_BROKEN_PIPE):GetLastError();
        CloseHandle(ov.hEvent);
        if(!ok||!done){SetLastError(error);return false;}
        count-=done;data+=done;
    }
    return true;
}
}
std::shared_ptr<Session> Session::Start(const std::wstring& exe,unsigned budgetMiB,std::ostream& log){
    auto out=std::shared_ptr<Session>(new Session);
    unsigned char random[16]{};
    if(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){
        log<<"AssetCache64: secure session token generation failed\n";
        return {};
    }
    std::wostringstream token;
    token<<std::hex<<std::setfill(L'0');for(auto byte:random)token<<std::setw(2)<<unsigned(byte);
    out->name_=token.str();
    std::wstring command=L"\""+exe+L"\" --asset-cache "+std::to_wstring(GetCurrentProcessId())+
        L" "+out->name_+L" "+std::to_wstring(std::clamp(budgetMiB,128u,32768u));
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                       nullptr,nullptr,&si,&pi)){
        log<<"AssetCache64: helper launch failed, Windows error="<<GetLastError()<<"\n";
        return {};
    }
    CloseHandle(pi.hThread);out->process_=pi.hProcess;out->pid_=pi.dwProcessId;
    // Startup can race the helper's runtime loader, antivirus scanning and
    // CreateNamedPipe. Diagnose each outcome instead of silently giving up.
    asc::native::Reply reply;
    const uint64_t until=GetTickCount64()+5000;
    bool exited=false;
    do {
        if(out->Stats(reply)){
            log<<"AssetCache64: x64 helper PID="<<out->pid_<<", budget MiB="<<budgetMiB
               <<", handshake=connected\n";
            return out;
        }
        const DWORD wait=WaitForSingleObject(pi.hProcess,50);
        if(wait==WAIT_OBJECT_0){exited=true;break;}
        if(wait==WAIT_FAILED){
            log<<"AssetCache64: helper process wait failed, error="<<GetLastError()<<"\n";
            break;
        }
    }while(GetTickCount64()<until);
    DWORD exitCode=STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess,&exitCode);
    log<<"AssetCache64: helper handshake failed, pid="<<out->pid_
       <<", stage="<<out->lastConnectStage_
       <<", Win32="<<out->lastConnectError_
       <<", process="<<(exited?"exited":"not ready")
       <<", exitCode="<<exitCode<<"\n";
    // The process was launched by this attempt and owns no successful backing.
    // Don't leak a waiting helper when a later retry generates a fresh session.
    if(!exited&&WaitForSingleObject(pi.hProcess,0)==WAIT_TIMEOUT){
        if(TerminateProcess(pi.hProcess,1))
            WaitForSingleObject(pi.hProcess,1000);
    }
    return {};
}
Session::~Session(){
    if(transferView_)UnmapViewOfFile(transferView_);
    if(transfer_)CloseHandle(transfer_);
    if(process_){
        asc::native::Reply reply;
        // Do not send a Stop handshake if the helper already exited.
        if(WaitForSingleObject(process_,0)==WAIT_TIMEOUT)
            Exchange(asc::native::Command::Stop,0,0,reply);
        if(pipe_!=INVALID_HANDLE_VALUE){CloseHandle(pipe_);pipe_=INVALID_HANDLE_VALUE;}
        CloseHandle(process_);
    }
}
std::pair<const char*,DWORD> Session::LastTransportError(){
    std::lock_guard guard(mutex_);
    return {lastConnectStage_,lastConnectError_};
}
bool Session::Exchange(asc::native::Command cmd,uint64_t id,uint64_t bytes,asc::native::Reply& out,
    const void* payload,DWORD payloadBytes,void* extra,DWORD extraBytes){
    std::lock_guard guard(mutex_);
    auto fail=[&](const char* stage,DWORD error){
        lastConnectStage_=stage;
        lastConnectError_=error;
        if(pipe_!=INVALID_HANDLE_VALUE){
            CloseHandle(pipe_);
            pipe_=INVALID_HANDLE_VALUE;
        }
        SetLastError(error);
        return false;
    };
    if(!process_)return fail("no process",ERROR_INVALID_HANDLE);
    if(WaitForSingleObject(process_,0)!=WAIT_TIMEOUT)
        return fail("helper exited",ERROR_PROCESS_ABORTED);

    // A session owns one duplex pipe for the lifetime of its connection.
    // Reopening the one-instance server on EVERY D3D resource operation was
    // racing DisconnectNamedPipe and produced ERROR_PIPE_BUSY (231).
    // Transport failures invalidate the connection; the next command may
    // reconnect. We never replay an ambiguous resource-allocation command.
    if(pipe_==INVALID_HANDLE_VALUE){
        const auto pipeName=asc::native::PipeName(name_);
        DWORD error=ERROR_FILE_NOT_FOUND;
        for(unsigned retry=0;retry<10;++retry){
            pipe_=CreateFileW(pipeName.c_str(),GENERIC_READ|GENERIC_WRITE,
                              0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
            if(pipe_!=INVALID_HANDLE_VALUE)break;
            error=GetLastError();
            if(error!=ERROR_PIPE_BUSY&&error!=ERROR_FILE_NOT_FOUND)
                return fail("open named pipe",error);
            // The x64 service may still be starting, or disconnecting an
            // earlier transport. Never spin; startup runs on a worker.
            if(error==ERROR_PIPE_BUSY){
                if(!WaitNamedPipeW(pipeName.c_str(),200)){
                    error=GetLastError();
                    if(error!=ERROR_SEM_TIMEOUT&&error!=ERROR_PIPE_BUSY&&error!=ERROR_FILE_NOT_FOUND)
                        return fail("wait named pipe",error);
                }
            }else Sleep(50);
            if(WaitForSingleObject(process_,0)!=WAIT_TIMEOUT)
                return fail("helper exited while waiting for pipe",ERROR_PROCESS_ABORTED);
        }
        if(pipe_==INVALID_HANDLE_VALUE)
            return fail("open named pipe retries exhausted",error);

        ULONG server=0;
        if(!GetNamedPipeServerProcessId(pipe_,&server))
            return fail("identify helper pipe server",GetLastError());
        if(server!=pid_)return fail("unexpected pipe server PID",ERROR_ACCESS_DENIED);
    }
    asc::native::Request req;req.command=cmd;req.id=id;req.bytes=bytes;
    // All request/reply stages and partial transfers consume one deadline.
    const uint64_t deadline=GetTickCount64()+asc::native::RpcTimeoutMs;
    if(!Transfer(pipe_,&req,sizeof(req),true,deadline))
        return fail("write request",GetLastError());
    if(payloadBytes&&!Transfer(pipe_,const_cast<void*>(payload),payloadBytes,true,deadline))
        return fail("write payload",GetLastError());
    if(!Transfer(pipe_,&out,sizeof(out),false,deadline))
        return fail("read reply",GetLastError());
    if(out.magic!=asc::native::Magic)
        return fail("protocol magic mismatch",ERROR_INVALID_DATA);
    if(extraBytes&&out.status==0&&!Transfer(pipe_,extra,extraBytes,false,deadline))
        return fail("read reply payload",GetLastError());
    // No per-command ACK/disconnect: the helper reads the next request from
    // this same connection. A failed command is a valid reply, not a broken IPC.
    if(out.status!=0){
        lastConnectStage_="helper rejected command";
        lastConnectError_=ERROR_INVALID_DATA;
        return false;
    }
    lastConnectStage_="connected";
    lastConnectError_=ERROR_SUCCESS;
    return true;
}
uint64_t Session::NextId(){std::lock_guard guard(mutex_);return next_++;}
std::unique_ptr<Backing> Session::Allocate(uint64_t bytes){
    // Resource creation is on the D3D device thread. IDs are nevertheless
    // protected independently from the transport to tolerate worker callers.
    const uint64_t id=NextId();
    asc::native::Reply reply;
    if(!bytes||bytes>asc::native::MaxResource||!Exchange(asc::native::Command::Allocate,id,bytes,reply))return {};
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,asc::native::MappingName(name_,id).c_str());
    if(!mapping){Release(id);return {};}
    try {return std::make_unique<Backing>(shared_from_this(),id,bytes,mapping);}
    catch(const std::bad_alloc&){CloseHandle(mapping);Release(id);return {};}
}
void Session::Release(uint64_t id){asc::native::Reply reply;Exchange(asc::native::Command::Release,id,0,reply);}
bool Session::Stats(asc::native::Reply& out){return Exchange(asc::native::Command::Stats,0,0,out);}
bool Session::AssetStats(asc::native::AssetSummary& out){
    asc::native::Reply reply;
    return Exchange(asc::native::Command::AssetStats,0,0,reply,nullptr,0,&out,sizeof(out));
}
bool Session::ReadAsset(HANDLE source,uint64_t offset,void* output,DWORD bytes,DWORD& done){
    done=0;if(!output||!bytes||bytes>asc::native::MaxResource)return false;
    std::lock_guard guard(assetMutex_);
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(source,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    asc::native::Reply reply;
    auto file=sources_.find(source);
    if(file!=sources_.end()&&(file->second.volume!=info.dwVolumeSerialNumber||
        file->second.high!=info.nFileIndexHigh||file->second.low!=info.nFileIndexLow)){
        Exchange(asc::native::Command::CloseSource,file->second.id,0,reply);sources_.erase(file);file=sources_.end();
    }
    try {
        if(file==sources_.end()){
            const uint64_t id=NextId();
            if(!Exchange(asc::native::Command::OpenSource,id,uint64_t(uintptr_t(source)),reply))return false;
            try {file=sources_.emplace(source,Source{id,info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow}).first;}
            catch(...){Exchange(asc::native::Command::CloseSource,id,0,reply);throw;}
        }
        if(!transfer_){
            transferId_=NextId();
            if(!Exchange(asc::native::Command::Allocate,transferId_,asc::native::TransferBytes,reply))return false;
            transfer_=OpenFileMappingW(FILE_MAP_READ,FALSE,asc::native::MappingName(name_,transferId_).c_str());
            if(!transfer_){Release(transferId_);return false;}
        }
        // One small shared window regardless of archive or total cache size.
        if(!transferView_)
            transferView_=MapViewOfFile(transfer_,FILE_MAP_READ,0,0,asc::native::TransferBytes);
        if(!transferView_)return false;
        bool ok=true;
        while(done<bytes){
            const DWORD count=std::min<DWORD>(bytes-done,asc::native::TransferBytes);
            if(offset>UINT64_MAX-done){ok=false;break;}
            asc::native::AssetRead read{transferId_,offset+done};
            if(!Exchange(asc::native::Command::ReadSource,file->second.id,count,
                 reply,&read,sizeof(read))||reply.used>count){ok=false;break;}
            const DWORD got=DWORD(reply.used);
            std::memcpy(static_cast<unsigned char*>(output)+done,transferView_,got);done+=got;
            if(got<count)break;
        }
        return ok;
    }catch(const std::bad_alloc&){return false;}
}
bool Session::RegisterWarmSource(HANDLE source){
    if(!source||source==INVALID_HANDLE_VALUE)return false;
    const uint64_t id=NextId();
    asc::native::Reply reply{};
    // OpenWarmSource is exclusively for persistent warm-up handles.
    return Exchange(asc::native::Command::OpenWarmSource,
                    id,uint64_t(uintptr_t(source)),reply);
}
bool Session::RegisterVerifiedImgMember(
    HANDLE selectedSource,const asc::native::ObservedImgMemberWarm& observed,
    uint64_t& memberOffset,uint64_t& memberBytes){
    memberOffset=0;memberBytes=0;
    if(!selectedSource || selectedSource==INVALID_HANDLE_VALUE)return false;
    asc::native::Reply reply{};
    if(!Exchange(asc::native::Command::OpenWarmImgMember,NextId(),
        uint64_t(uintptr_t(selectedSource)),reply,
        &observed,sizeof(observed)))return false;
    memberOffset=reply.resources; // specifically returned member start
    memberBytes=reply.used;       // specifically returned member length
    return memberBytes!=0;
}
bool Session::CacheCoverage(asc::native::CacheCoverage& out){
    asc::native::Reply reply{};
    return Exchange(asc::native::Command::CacheCoverage,0,0,reply,
                    nullptr,0,&out,sizeof(out));
}
bool Session::CatalogConfigure(bool enabled){
    asc::native::Reply reply{};
    return Exchange(asc::native::Command::CatalogConfigure,0,enabled?1:0,reply);
}
bool Session::CatalogStatus(asc::native::CatalogSummary& out){
    asc::native::Reply reply{};
    return Exchange(asc::native::Command::CatalogStatus,0,0,reply,
                    nullptr,0,&out,sizeof(out));
}
bool Session::CatalogQueryCell(int32_t x,int32_t y,asc::native::CatalogCellReply& out){
    asc::native::Reply reply{};
    const uint64_t key=(uint64_t(uint32_t(x))<<32)|uint32_t(y);
    return Exchange(asc::native::Command::CatalogQueryCell,key,0,reply,
                    nullptr,0,&out,sizeof(out));
}
void Session::CloseAssetSource(HANDLE source){
    std::lock_guard guard(assetMutex_);
    auto found=sources_.find(source);
    if(found==sources_.end())return;
    asc::native::Reply reply{};
    // Cache entries survive CloseSource; only the open file handle is closed.
    Exchange(asc::native::Command::CloseSource,found->second.id,0,reply);
    sources_.erase(found);
}
bool View::Map(HANDLE mapping,uint64_t offset,size_t bytes){
    Reset();if(!bytes)return false;
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const uint64_t aligned=offset-(offset%info.dwAllocationGranularity);
    const size_t delta=size_t(offset-aligned);
    if(bytes>SIZE_MAX-delta)return false;
    base_=MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,DWORD(aligned>>32),DWORD(aligned),delta+bytes);
    if(!base_)return false;
    data_=static_cast<unsigned char*>(base_)+delta;return true;
}
}
