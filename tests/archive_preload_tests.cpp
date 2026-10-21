#include "asc/ArchivePreload.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <array>
#include <cstring>
using namespace asc::archive_preload;
static void le32(std::array<unsigned char,32>& v,int i,unsigned x) {
    for(int j=0;j<4;++j)v[i+j]=static_cast<unsigned char>((x>>(j*8))&255);
}
int main(){
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/"asc_archive_preload_fixture";
    fs::remove_all(root);fs::create_directories(root/"models");
    // Valid IMG2 entry, offset 1 sector, length 1 sector, name zone.ipl.
    {
        std::ofstream f(root/"models"/"gta3.img",std::ios::binary);
        const char head[]={'V','E','R','2',1,0,0,0};
        f.write(head,sizeof head);
        std::array<unsigned char,32> entry{};
        le32(entry,0,1);entry[4]=1;entry[6]=1;
        std::memcpy(entry.data()+8,"zone.ipl",9);
        f.write(reinterpret_cast<const char*>(entry.data()),entry.size());
        std::array<char,2048> zero{};
        f.write(zero.data(),2048-8-32);
        zero[0]='b';zero[1]='n';zero[2]='r';zero[3]='y';
        f.write(zero.data(),zero.size());
    }
    Settings cfg{};
    cfg.enabled=true;cfg.binaryIpls=true;cfg.maxCacheMiB=16;
    cfg.maxEntryMiB=2;cfg.sleepBetweenReadsMs=1;
    Start(root,cfg);
    std::shared_ptr<const std::vector<uint8_t>> data;
    for(int n=0;n<300 && !data;++n) {
        data=TryGet("ZONE.IPL");
        if(!data)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(data && data->size()==2048);
    assert(std::memcmp(data->data(),"bnry",4)==0);
    assert(Snapshot().loaded==1);
    Stop();
    // Existing shared ownership survives cache eviction/shutdown.
    assert(data->at(0)=='b');
    fs::remove_all(root);
}
