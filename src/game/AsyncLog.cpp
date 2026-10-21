#include "AsyncLog.h"
#include <cstdio>
#include <cstring>

namespace {
void Write(HANDLE file,const char* bytes,DWORD size){
    while(size){
        DWORD written=0;
        if(!WriteFile(file,bytes,size,&written,nullptr)||!written)return;
        bytes+=written;size-=written;
    }
}
}
AsyncLog::AsyncLog():std::ostream(nullptr){
    setp(buffer_.data(),buffer_.data()+buffer_.size());
    std::ostream::rdbuf(this);
}
bool AsyncLog::Open(const std::string& path){
    if(thread_)return true;
    file_=CreateFileA(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,
                      CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file_==INVALID_HANDLE_VALUE){setstate(std::ios::badbit);return false;}
    event_=CreateEventA(nullptr,FALSE,FALSE,nullptr);
    if(event_){
        stopping_.store(false,std::memory_order_relaxed);
        thread_=CreateThread(nullptr,0,&Entry,this,0,nullptr);
    }
    if(!thread_){
        if(event_)CloseHandle(event_);
        event_=nullptr;CloseHandle(file_);file_=INVALID_HANDLE_VALUE;
        setstate(std::ios::badbit);
        return false;
    }
    clear();return true;
}
void AsyncLog::Publish(){
    const auto count=static_cast<DWORD>(pptr()-pbase());
    if(!count)return;
    if(thread_){
        Chunk chunk{};chunk.size=count;
        std::memcpy(chunk.bytes.data(),pbase(),count);
        if(!queue_.Push(chunk))dropped_.fetch_add(1,std::memory_order_relaxed);
        else SetEvent(event_);
    }
    setp(buffer_.data(),buffer_.data()+buffer_.size());
}
int AsyncLog::sync(){Publish();return 0;}
AsyncLog::int_type AsyncLog::overflow(int_type value){
    Publish();
    if(!traits_type::eq_int_type(value,traits_type::eof())){
        *pptr()=traits_type::to_char_type(value);pbump(1);
    }
    return traits_type::not_eof(value);
}
DWORD WINAPI AsyncLog::Entry(void* self){static_cast<AsyncLog*>(self)->Work();return 0;}
void AsyncLog::Work(){
    for(;;){
        WaitForSingleObject(event_,1000);
        // Snapshot stop BEFORE draining: Stop publishes the tail first.
        const bool stop=stopping_.load(std::memory_order_acquire);
        Chunk chunk{};
        while(queue_.Pop(chunk))Write(file_,chunk.bytes.data(),chunk.size);
        const auto dropped=dropped_.exchange(0,std::memory_order_relaxed);
        if(dropped){
            char message[128]{};
            const int size=std::snprintf(message,sizeof(message),
                "\nASC log: dropped %llu diagnostic chunks (writer backlog).\n",
                static_cast<unsigned long long>(dropped));
            if(size>0)Write(file_,message,static_cast<DWORD>(size));
        }
        if(stop)break;
    }
    FlushFileBuffers(file_);
}
void AsyncLog::Stop(){
    if(!thread_)return;
    Publish();
    stopping_.store(true,std::memory_order_release);SetEvent(event_);
    WaitForSingleObject(thread_,INFINITE);
    CloseHandle(thread_);thread_=nullptr;
    CloseHandle(event_);event_=nullptr;
    CloseHandle(file_);file_=INVALID_HANDLE_VALUE;
}
