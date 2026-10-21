#include "asc/WorldCatalog.h"
#include <cassert>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs=std::filesystem;
using asc::world_catalog::Catalog;
static void Write(const fs::path& p,const std::string& content){
    fs::create_directories(p.parent_path());
    std::ofstream f(p,std::ios::binary|std::ios::trunc);
    assert(f.good());
    f.write(content.data(),std::streamsize(content.size()));
    assert(f.good());
}
static void Put32(std::string& str,size_t at,uint32_t v){
    for(unsigned k=0;k<4;++k)str[at+k]=char(v>>(8*k));
}
static void PutFloat(std::string& str,size_t at,float f){
    Put32(str,at,std::bit_cast<uint32_t>(f));
}
int main(){
    auto dir=fs::temp_directory_path()/
        ("ds_world_disk_test_"+std::to_string(std::chrono::steady_clock::now()
          .time_since_epoch().count()));
    auto snapshot=dir/"cache"/"world.bin";
    try {
        Write(dir/"data/gta.dat",
            "IDE data/test.ide\nIPL data/test.ipl\nIMG models/world.img\n");
        auto ide=std::string("objs\n101,tree_test,generic,1,200.0,0\nend\n");
        auto ipl=std::string("inst\n101,tree_test,0,5,2,3,0,0,0,1,-1\nend\n");
        Write(dir/"data/test.ide",ide);
        Write(dir/"data/test.ipl",ipl);
        std::string binary(4096,'\0');
        std::memcpy(binary.data(),"VER2",4);
        Put32(binary,4,2);
        Put32(binary,8,1);
        binary[12]=1; // one sector
        std::memcpy(binary.data()+16,"region0.ipl",11);
        Put32(binary,40,0); // second entry has no payload -> rejects candidate
        Put32(binary,40,1);
        binary[44]=1;
        std::memcpy(binary.data()+48,"tree_test.dff",13);
        // binary IPL must not overwrite the IMG directory or DFF test entry
        std::memcpy(binary.data()+2048,"bnry",4);
        Put32(binary,2052,1);
        Put32(binary,2076,76);
        PutFloat(binary,2048+76,10.f);
        PutFloat(binary,2048+80,20.f);
        PutFloat(binary,2048+84,30.f);
        PutFloat(binary,2048+100,1.f);
        Put32(binary,2048+104,101);
        Put32(binary,2048+112,0xffffffff);
        Write(dir/"models/world.img",binary);
        auto a=Catalog::LoadOrBuild(dir,snapshot);
        assert(a.GetStatus().ready&&!a.GetStatus().loadedFromCache);
        assert(fs::exists(snapshot));
        assert(a.GetStatus().placements==2);
        assert(a.Query(0,0).modelIds.size()==1);
        auto b=Catalog::LoadOrBuild(dir,snapshot);
        assert(b.GetStatus().loadedFromCache);
        assert(b.GetStatus().manifestHash==a.GetStatus().manifestHash);
        assert(b.Query(0,0).modelIds.size()==1);
        assert(b.Dependency("tree_test")!=nullptr);
        assert(b.CandidateAssets("tree_test.dff")!=nullptr);
        // Same-size content edits are detected even when timestamps revert.
        auto ideStamp=fs::last_write_time(dir/"data/test.ide");
        auto changed=ide;changed.replace(changed.find("101,"),3,"102");
        Write(dir/"data/test.ide",changed);
        fs::last_write_time(dir/"data/test.ide",ideStamp);
        auto c=Catalog::LoadOrBuild(dir,snapshot);
        assert(!c.GetStatus().loadedFromCache);
        assert(c.GetStatus().resolved==0); // numeric IPL IDs no longer match
        assert(Catalog::LoadOrBuild(dir,snapshot).GetStatus().loadedFromCache);
        Write(dir/"data/test.ide",ide);
        auto d=Catalog::LoadOrBuild(dir,snapshot);
        assert(!d.GetStatus().loadedFromCache);
        assert(d.GetStatus().resolved==2);
        // Change an archived binary IPL position at the same file size/time.
        const auto imgStamp=fs::last_write_time(dir/"models/world.img");
        auto moved=binary;
        PutFloat(moved,2048+76,999.f);
        Write(dir/"models/world.img",moved);
        fs::last_write_time(dir/"models/world.img",imgStamp);
        auto e=Catalog::LoadOrBuild(dir,snapshot);
        assert(!e.GetStatus().loadedFromCache);
        assert(e.Query(3,0).placements==1);
        assert(Catalog::LoadOrBuild(dir,snapshot).GetStatus().loadedFromCache);
        // Same-size archive directory name edits invalidate candidates.
        auto renamed=moved;
        std::memcpy(renamed.data()+48,"othername.dff",13);
        Write(dir/"models/world.img",renamed);
        fs::last_write_time(dir/"models/world.img",imgStamp);
        auto f=Catalog::LoadOrBuild(dir,snapshot);
        assert(!f.GetStatus().loadedFromCache);
        assert(f.CandidateAssets("tree_test.dff")==nullptr);
        assert(f.CandidateAssets("othername.dff")!=nullptr);
        // Mutation of the snapshot itself cannot become authoritative.
        std::ifstream input(snapshot,std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
        input.close(); // Permit Windows to replace the snapshot below.
        assert(raw.size()>50);
        raw[47]=char(raw[47]^0x5a);
        Write(snapshot,raw);
        auto rebuilt=Catalog::LoadOrBuild(dir,snapshot);
        assert(!rebuilt.GetStatus().loadedFromCache);
        assert(rebuilt.Query(3,0).placements==1);
        assert(Catalog::LoadOrBuild(dir,snapshot).GetStatus().loadedFromCache);
        Write(snapshot,"truncated");
        assert(!Catalog::LoadOrBuild(dir,snapshot).GetStatus().loadedFromCache);
        assert(Catalog::LoadOrBuild(dir,snapshot).GetStatus().loadedFromCache);
        // Cell size changes require a distinct cache identity.
        assert(!Catalog::LoadOrBuild(dir,snapshot,128).GetStatus().loadedFromCache);
        assert(Catalog::LoadOrBuild(dir,snapshot,128).GetStatus().loadedFromCache);
    }catch(...){fs::remove_all(dir);throw;}
    fs::remove_all(dir);
    std::cout<<"World catalog disk snapshot tests passed\n";
}
