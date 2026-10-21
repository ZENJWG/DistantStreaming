#include "asc/WorldCatalog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <cstring>
#include <bit>
#include <vector>

namespace fs=std::filesystem;
static void Write(const fs::path& file,const std::string& text){
    fs::create_directories(file.parent_path());
    std::ofstream out(file,std::ios::binary);
    assert(out.good());
    out<<text;
}
int main(){
    auto directory=fs::temp_directory_path()/
        ("ds_catalog_test_"+std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count()));
    fs::create_directories(directory);
    try{
        auto empty=asc::world_catalog::Catalog::Build(directory);
        assert(!empty.GetStatus().ready && empty.GetStatus().rejected>0);
        Write(directory/"data/gta.dat",
            "IDE data/maps/test.ide\n"
            "IPL data/maps/test.ipl\n"
            "IPL ../../outside.ipl\n"
            "IPL data/maps/missing_binary.ipl\n");
        Write(directory/"data/maps/test.ide",
            "objs\n"
            "101, tree_test, generic, 1, 200.0, 0\n"
            "102, object_test, generic, 1, 200.0, 0\n"
            "end\n"
            "tobj\n103, street_light, generic, 1, 200.0,0,6,20\nend\n");
        Write(directory/"data/maps/test.ipl",
            "inst\n"
            "101,tree_test,0,1.0,2.0,3.0,0,0,0,1,-1\n"
            "102,object_test,0,-1.0,-257.0,3.0,0,0,0,1,-1\n"
            "103,street_light,0,1.0,2.0,4.0,0,0,0,1,-1\n"
            "105,missing_model,0,1.0,2.0,3.0,0,0,0,1,-1\n"
            "106,too_few_columns,0,0,0,0\n"
            "107,nan,0,nan,0,0,0,0,0,1,-1\n"
            "end\n");
        auto a=asc::world_catalog::Catalog::Build(directory);
        auto b=asc::world_catalog::Catalog::Build(directory);
        const auto& s=a.GetStatus();
        assert(s.ready && !s.complete);
        assert(s.sources==2 && s.ideDefinitions==3 && s.placements==4);
        assert(s.resolved==3 && s.unresolved==3); // missing path, unsafe path, model
        assert(s.rejected==2 && s.cells==2);
        assert(s.manifestHash==b.GetStatus().manifestHash);
        const auto cell=a.Query(0,0);
        assert(cell.placements==3 && cell.modelIds.size()==2);
        assert(cell.modelIds[0]==101 && cell.modelIds[1]==103);
        assert(cell.txdNames.size()==cell.modelNames.size());
        assert(std::string(cell.txdNames[0].data())=="generic");
        const auto negative=a.Query(-1,-2);
        assert(negative.placements==1 && negative.modelIds.size()==1);
        assert(negative.modelIds[0]==102);
        assert(a.Query(400,400).modelIds.empty());
        assert(a.Query(0,0,1).modelIds.size()==1);
        // A same-size change must invalidate catalog generation, not reuse stale placements.
        Write(directory/"data/maps/test.ide",
            "objs\n"
            "201, tree_test, generic, 1, 200.0, 0\n"
            "102, object_test, generic, 1, 200.0, 0\n"
            "end\n"
            "tobj\n103, street_light, generic, 1, 200.0,0,6,20\nend\n");
        auto changed=asc::world_catalog::Catalog::Build(directory);
        assert(changed.GetStatus().generation!=s.generation);
        assert(changed.Query(0,0).modelIds[0]==103); // 101 IPL ID no longer matches IDE 201
        // Binary files are counted unsupported, never interpreted as fake text instances.
        Write(directory/"data/maps/test.ipl",std::string("inst\n")+
            std::string(100,'\0'));
        auto binary=asc::world_catalog::Catalog::Build(directory);
        assert(binary.GetStatus().unsupported==1 && binary.GetStatus().placements==0);
        // Valid GTA SA binary IPL: 76-byte header, 40-byte inst record.
        auto put32=[](std::string& b,size_t at,uint32_t v){
            for(size_t i=0;i<4;++i)b[at+i]=char((v>>(8*i))&255u);
        };
        auto putFloat=[&](std::string& b,size_t at,float f){
            put32(b,at,std::bit_cast<uint32_t>(f));
        };
        std::string binaryIpl(116,'\0');
        std::memcpy(binaryIpl.data(),"bnry",4);
        put32(binaryIpl,4,1); // instances
        put32(binaryIpl,28,76); // instance offset
        putFloat(binaryIpl,76,12.5f);
        putFloat(binaryIpl,80,-2.0f);
        putFloat(binaryIpl,84,3.0f);
        putFloat(binaryIpl,100,1.0f); // quaternion w
        put32(binaryIpl,104,103); // ID at byte 28 of record
        put32(binaryIpl,112,0xffffffff); // LOD -1
        Write(directory/"data/maps/test.ipl",binaryIpl);
        auto direct=asc::world_catalog::Catalog::Build(directory);
        assert(direct.GetStatus().binarySources==1);
        assert(direct.GetStatus().binaryPlacements==1);
        assert(direct.Query(0,-1).modelIds.size()==1);
        assert(direct.Query(0,-1).modelNames[0][0]=='s');
        // Same IMG v2 archive layout used in existing ArchivePreload indexer.
        std::string image(8192,'\0');
        std::memcpy(image.data(),"VER2",4);
        put32(image,4,3);   // IPL + DFF + TXD source candidates
        put32(image,8,1);
        image[12]=1;
        std::memcpy(image.data()+16,"region_stream0.ipl",18);
        put32(image,40,2);
        image[44]=1;
        std::memcpy(image.data()+48,"tree_test.dff",13);
        put32(image,72,3);
        image[76]=1;
        std::memcpy(image.data()+80,"generic.txd",12);
        std::memcpy(image.data()+2048,binaryIpl.data(),binaryIpl.size());
        Write(directory/"models/gta3.img",image);
        auto archived=asc::world_catalog::Catalog::Build(directory);
        assert(archived.GetStatus().imgArchives==1);
        assert(archived.GetStatus().binarySources==2);
        assert(archived.GetStatus().binaryPlacements==2);
        assert(archived.GetStatus().archiveAssetCandidates==2);
        assert(archived.GetStatus().modelsWithArchiveCandidates>=1);
        const auto* dep=archived.Dependency("tree_test");
        assert(dep && dep->txdName=="generic" && dep->dffCandidates==1
               && dep->txdCandidates==1);
        const auto* dff=archived.CandidateAssets("tree_test.dff");
        assert(dff && dff->size()==1 &&
               dff->front().type==asc::world_catalog::AssetType::Dff);
        assert(archived.CandidateAssets("missing.dff")==nullptr);
        // Filename/extent changes inside a same-size IMG directory MUST
        // invalidate the catalog generation, even when IPL bytes are stable.
        std::string renamedImage=image;
        std::memcpy(renamedImage.data()+48,"othername.dff",13);
        Write(directory/"models/gta3.img",renamedImage);
        auto changedArchive=asc::world_catalog::Catalog::Build(directory);
        assert(changedArchive.GetStatus().generation!=archived.GetStatus().generation);
        assert(changedArchive.Dependency("tree_test")->dffCandidates==0);
        Write(directory/"models/gta3.img",image);
        // Reject truncated instance array without accepting a partial result.
        put32(binaryIpl,4,100000);
        Write(directory/"data/maps/test.ipl",binaryIpl);
        auto corrupt=asc::world_catalog::Catalog::Build(directory);
        assert(corrupt.GetStatus().unsupported>=1);
        assert(corrupt.GetStatus().binaryPlacements==1);
        // A malicious path outside gameRoot must never be followed.
        fs::remove_all(directory/"data/maps");
        auto c=asc::world_catalog::Catalog::Build(directory);
        assert(c.GetStatus().placements==1 && c.GetStatus().unresolved>=4); // retained IMG binary IPL
    }catch(...){
        fs::remove_all(directory);
        throw;
    }
    fs::remove_all(directory);
    std::cout<<"World catalog fixtures passed\n";
    return 0;
}
