#include "asc/WorldCatalog.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <cstring>
#include <array>
#include <system_error>
#include <climits>
#include <unordered_set>

namespace asc::world_catalog {
namespace {
constexpr size_t kMaxFileBytes=64ull*1024*1024;
constexpr size_t kMaxTotalPlacements=1'500'000;
constexpr size_t kMaxManifestEntries=8192;
constexpr size_t kMaxAssetCandidates=200000;
constexpr uint64_t kBasis=14695981039346656037ull;
std::string Trim(std::string value) {
    auto valid=[](unsigned char c){return !std::isspace(c);};
    auto a=std::find_if(value.begin(),value.end(),valid);
    auto b=std::find_if(value.rbegin(),value.rend(),valid).base();
    if(a>=b)return {};
    return std::string(a,b);
}
std::string Lower(std::string value) {
    for(char& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
uint64_t Mix(uint64_t h,std::string_view value) {
    for(unsigned char c:value){h^=c;h*=1099511628211ull;}
    return h;
}
std::vector<std::string> Split(std::string_view value) {
    std::vector<std::string> tokens;
    if(value.size()>4096)return tokens;
    size_t start=0;
    while(start<=value.size() && tokens.size()<32){
        auto end=value.find(',',start);
        if(end==std::string_view::npos)end=value.size();
        tokens.push_back(Trim(std::string(value.substr(start,end-start))));
        if(end==value.size())break;
        start=end+1;
    }
    return tokens;
}
bool Int(std::string_view word,int32_t& out){
    if(word.empty()||word.size()>32)return false;
    std::string copy(word);char* end=nullptr;
    const long value=std::strtol(copy.c_str(),&end,10);
    if(end==copy.c_str()||*end||value<std::numeric_limits<int32_t>::min()||
       value>std::numeric_limits<int32_t>::max())return false;
    out=int32_t(value);return true;
}
bool Float(std::string_view word,float& out){
    if(word.empty()||word.size()>32)return false;
    std::string copy(word);char* end=nullptr;
    const float value=std::strtof(copy.c_str(),&end);
    if(end==copy.c_str()||*end||!std::isfinite(value)||
       std::fabs(value)>1000000.0f)return false;
    out=value;return true;
}
bool FitsCell(float f,int32_t units,int32_t& cell){
    double x=std::floor(double(f)/double(units));
    if(x<double(INT32_MIN)||x>double(INT32_MAX))return false;
    cell=int32_t(x);return true;
}
bool UnderRoot(const std::filesystem::path& root,const std::filesystem::path& path){
    // Reject traversal, absolute paths, symlinks and anything escaping game root.
    auto a=root.lexically_normal();
    auto b=path.lexically_normal();
    auto it=b.begin();
    for(const auto& component:a){
        if(it==b.end()||Lower(it->string())!=Lower(component.string()))return false;
        ++it;
    }
    for(;it!=b.end();++it)if(*it=="..")return false;
    return true;
}
bool Resolve(const std::filesystem::path& root,std::string relative,
             std::filesystem::path& result,
             uint64_t maxBytes=kMaxFileBytes) {
    if(relative.empty()||relative.size()>512)return false;
    // Paths in gta.dat are traditionally Windows-style.
    std::replace(relative.begin(),relative.end(),'\\','/');
    auto fragment=std::filesystem::path(relative);
    if(fragment.is_absolute()||fragment.has_root_name())return false;
    for(auto part:fragment)if(part=="..")return false;
    auto path=(root/fragment).lexically_normal();
    if(!UnderRoot(root,path))return false;
    std::error_code ec;
    // canonical protects against symlink escapes, including inside gameRoot.
    auto canonicalRoot=std::filesystem::weakly_canonical(root,ec);
    if(ec)return false;
    auto canonicalPath=std::filesystem::weakly_canonical(path,ec);
    if(ec||!UnderRoot(canonicalRoot,canonicalPath))return false;
    if(!std::filesystem::is_regular_file(canonicalPath,ec)||ec)return false;
    if(std::filesystem::file_size(canonicalPath,ec)>maxBytes||ec)return false;
    result=canonicalPath;return true;
}
Category Classify(std::string_view model,std::string_view path,bool timed){
    if(timed)return Category::Timed;
    const auto text=Lower(std::string(model)+" "+std::string(path));
    if(text.find("tree")!=std::string::npos||text.find("veg")!=std::string::npos||
       text.find("plant")!=std::string::npos||text.find("bush")!=std::string::npos)
        return Category::Vegetation;
    if(text.find("lod")!=std::string::npos)return Category::Lod;
    if(text.find("generic")!=std::string::npos)return Category::Generic;
    return Category::Normal; // ordinary static IDE objs category
}

uint16_t U16(const unsigned char* p) {
    return uint16_t(p[0]) | (uint16_t(p[1])<<8);
}
uint32_t U32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1])<<8) |
        (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
}
float F32(const unsigned char* p) {
    const uint32_t bits=U32(p);
    float value=0;
    static_assert(sizeof(value)==sizeof(bits));
    std::memcpy(&value,&bits,sizeof(value));
    return value;
}
bool CopyName(std::string_view name,std::array<char,24>& out){
    if(name.empty()||name.size()>=out.size())return false;
    for(const unsigned char c:name)
        if(!(std::isalnum(c)||c=='_'))return false;
    std::copy(name.begin(),name.end(),out.begin());
    return true;
}
struct BinaryJob {
    std::filesystem::path archive;
    std::string name;
    uint64_t offset=0,length=0;
};
// GTA SA VER2 format, sector offsets and bounded streaming entry sizes.
// Effective Mod Loader IMG replacement order cannot be inferred from directory order.
void IndexImg(const std::filesystem::path& archive,
              std::vector<BinaryJob>& jobs,
              std::unordered_map<std::string,std::vector<ArchiveCandidate>>& assets,
              uint64_t& assetCount,uint64_t& manifestHash,
              uint64_t& rejected,uint64_t& indexed){
    std::error_code ec;
    const auto size=std::filesystem::file_size(archive,ec);
    if(ec||size<8)return;
    std::ifstream in(archive,std::ios::binary);
    unsigned char header[8]{};
    if(!in.read(reinterpret_cast<char*>(header),8)||
       std::memcmp(header,"VER2",4)!=0)return;
    const uint32_t count=U32(header+4);
    if(count>150000||8ull+uint64_t(count)*32ull>size){++rejected;return;}
    ++indexed;
    for(uint32_t i=0;i<count;++i){
        unsigned char record[32]{};
        if(!in.read(reinterpret_cast<char*>(record),32)){++rejected;return;}
        const uint64_t at=uint64_t(U32(record))*2048ull;
        const uint64_t sectors=U16(record+6)?U16(record+6):U16(record+4);
        const uint64_t bytes=sectors*2048ull;
        const char* name=reinterpret_cast<const char*>(record+8);
        const auto* end=static_cast<const char*>(std::memchr(name,0,24));
        if(!end)end=name+24;
        while(end>name && end[-1]==' ')--end;
        const std::string low=Lower(std::string(name,end));
        if(low.find_first_of("/\\\\:")!=std::string::npos ||
           low.size()>32||!bytes||bytes>kMaxFileBytes ||
           at>size||bytes>size-at){++rejected;continue;}
        // The directory is small compared with IMG contents; fingerprint
        // candidate name and extent so a same-size directory rewrite changes
        // the catalog generation. This is not a full source-content checksum.
        manifestHash=Mix(manifestHash,low);
        manifestHash=Mix(manifestHash,std::to_string(at));
        manifestHash=Mix(manifestHash,std::to_string(bytes));
        if(low.ends_with(".ipl")){
            if(jobs.size()<50000)jobs.push_back({archive,low,at,bytes});
            else ++rejected;
        }else{
            AssetType type;
            if(low.ends_with(".dff"))type=AssetType::Dff;
            else if(low.ends_with(".txd"))type=AssetType::Txd;
            else if(low.ends_with(".col"))type=AssetType::Col;
            else continue;
            if(assetCount>=kMaxAssetCandidates){++rejected;continue;}
            assets[low].push_back({archive.string(),at,bytes,type});
            ++assetCount;
        }
    }
}
bool BinaryPlacements(const std::vector<unsigned char>& bytes,
                     const std::string& origin,
                     int32_t cellUnits,
                     const std::unordered_map<int32_t,std::string>& idNames,
                     const std::unordered_map<std::string,int32_t>& nameIds,
                     std::vector<Placement>& placements,
                     std::unordered_map<uint64_t,std::vector<uint32_t>>& cells,
                     Status& status){
    // Header: bnry + 18 dwords. Instance section is an array of 40-byte
    // GTA SA records: 7 floats (xyz, quaternion), ID, interior/flags, LOD.
    if(bytes.size()<76||std::memcmp(bytes.data(),"bnry",4)!=0)return false;
    const uint32_t n=U32(bytes.data()+4);
    const uint32_t offset=U32(bytes.data()+28);
    if(n>500000||offset<76||offset>bytes.size()||
       uint64_t(n)*40ull>bytes.size()-offset ||
       uint64_t(n)>kMaxTotalPlacements-placements.size())return false;
    // Validate the entire section *before* publishing any placements.
    for(uint32_t i=0;i<n;++i){
        const auto* r=bytes.data()+offset+uint64_t(i)*40ull;
        for(unsigned j=0;j<7;++j){
            const float value=F32(r+j*4);
            if(!std::isfinite(value)||std::fabs(value)>1000000.f)return false;
        }
        int32_t cx=0,cy=0;
        if(!FitsCell(F32(r),cellUnits,cx)||!FitsCell(F32(r+4),cellUnits,cy))
            return false;
        const int32_t model=static_cast<int32_t>(U32(r+28));
        if(model<0||model>1000000)return false;
    }
    for(uint32_t i=0;i<n;++i){
        const auto* r=bytes.data()+offset+uint64_t(i)*40ull;
        const int32_t model=static_cast<int32_t>(U32(r+28));
        Placement placement{};
        placement.x=F32(r);placement.y=F32(r+4);placement.z=F32(r+8);
        FitsCell(placement.x,cellUnits,placement.cellX);
        FitsCell(placement.y,cellUnits,placement.cellY);
        placement.identity=Mix(Mix(kBasis,origin),std::to_string(i));
        // No live game engine IDs are trusted. A numeric ID without one
        // unambiguous IDE name stays cataloged, but is never prefetchable.
        const auto name=idNames.find(model);
        if(name!=idNames.end() && !name->second.empty() &&
           nameIds.contains(name->second) && nameIds.at(name->second)==model &&
           CopyName(name->second,placement.modelName)){
            placement.modelId=model;
            placement.category=Classify(name->second,origin,false);
            ++status.resolved;
        }else {
            ++status.unresolved;
            if(name!=idNames.end() && name->second.empty())++status.ambiguousModels;
        }
        auto ordinal=uint32_t(placements.size());
        placements.push_back(placement);
        cells[CellKey(placement.cellX,placement.cellY)].push_back(ordinal);
    }
    ++status.binarySources;status.binaryPlacements+=n;
    return true;
}
} // namespace
uint64_t CellKey(int32_t x,int32_t y) noexcept {
    return (uint64_t(uint32_t(x))<<32)|uint32_t(y);
}
Catalog Catalog::Build(const std::filesystem::path& gameRoot,int32_t cellUnits){
    Catalog catalog;
    auto& status=catalog.status_;
    if(cellUnits<16||cellUnits>4096){status.rejected++;return catalog;}
    const auto root=gameRoot.lexically_normal();
    std::filesystem::path dat;
    if(!Resolve(root,"data/gta.dat",dat)){status.rejected++;return catalog;}
    std::ifstream manifest(dat);
    if(!manifest){status.rejected++;return catalog;}
    // Text-file catalog is only an inventory; effective Mod Loader overrides
    // and binary IPL remain unknown, therefore 'complete' stays false.
    status.manifestHash=kBasis;
    std::vector<std::filesystem::path> ideFiles,iplFiles,imgFiles;
    std::string line;
    size_t lines=0;
    while(std::getline(manifest,line)&&++lines<=kMaxManifestEntries){
        status.manifestHash=Mix(status.manifestHash,line);
        if(lines==1 && line.rfind("\xEF\xBB\xBF",0)==0)line.erase(0,3);
        if(line.size()>4096){status.rejected++;continue;}
        const auto pos=line.find_first_of("#;");
        if(pos!=std::string::npos)line.erase(pos);
        line=Trim(line);
        if(line.empty())continue;
        auto split=line.find_first_of(" \t");
        if(split==std::string::npos)continue;
        const auto type=Lower(line.substr(0,split));
        if(type!="ide"&&type!="ipl"&&type!="img")continue;
        auto pathText=Trim(line.substr(split+1));
        std::filesystem::path path;
        if(!Resolve(root,pathText,path,type=="img"?64ull*1024*1024*1024:kMaxFileBytes)){status.unresolved++;continue;}
        status.manifestHash=Mix(status.manifestHash,Lower(pathText));
        std::error_code ec;
        const auto size=std::filesystem::file_size(path,ec);
        status.manifestHash^=size;status.manifestHash*=1099511628211ull;
        if(type=="ide")ideFiles.push_back(path);
        else if(type=="ipl")iplFiles.push_back(path);
        else imgFiles.push_back(path);
    }
    if(!manifest.eof()){status.rejected++;}
    struct Definition {int32_t id;Category category;std::string txd;};
    std::unordered_map<std::string,Definition> definitions;
    std::unordered_map<int32_t,std::string> idNames;
    std::unordered_map<std::string,int32_t> nameIds;
    auto process=[&](const std::filesystem::path& path,bool ide) {
        std::ifstream file(path,std::ios::binary);
        if(!file){status.unresolved++;return;}
        ++status.sources;
        char probe[4096]{};
        file.read(probe,sizeof(probe));
        const auto count=file.gcount();
        if(count>=4 && std::memcmp(probe,"bnry",4)==0 && !ide){
            // Standalone binary IPL; archive sector padding is not required.
            const auto bytes=std::filesystem::file_size(path);
            if(bytes>kMaxFileBytes){++status.unsupported;return;}
            std::vector<unsigned char> data(size_t(bytes),0);
            file.clear();file.seekg(0);
            if(!file.read(reinterpret_cast<char*>(data.data()),
                          std::streamsize(data.size())) ||
               !BinaryPlacements(data,Lower(path.string()),cellUnits,idNames,
                  nameIds,catalog.placements_,catalog.cells_,status)){
                ++status.unsupported;return;
            }
            for(auto c:data){status.manifestHash^=c;status.manifestHash*=1099511628211ull;}
            return;
        }
        if(std::find(probe,probe+count,'\0')!=probe+count){
            ++status.unsupported;return; // unknown binary
        }
        file.clear();file.seekg(0);
        std::string section;
        size_t lineNo=0;
        while(std::getline(file,line) && lineNo<4'000'000){
            ++lineNo;
            status.manifestHash=Mix(status.manifestHash,line);
            if(lineNo==1 && line.rfind("\xEF\xBB\xBF",0)==0)line.erase(0,3);
            if(line.size()>4096){status.rejected++;continue;}
            const auto comment=line.find_first_of("#;");
            if(comment!=std::string::npos)line.erase(comment);
            line=Trim(line);
            if(line.empty())continue;
            const auto sectionName=Lower(line);
            if(sectionName=="end"){section.clear();continue;}
            if(line.find(',')==std::string::npos){
                // Recognized sections only. Binary IPL is *not* parsed.
                section=sectionName;
                continue;
            }
            if(ide){
                if(section!="objs"&&section!="tobj"&&section!="anim"&&section!="cars"&&section!="peds")continue;
                auto fields=Split(line);
                int32_t id=-1;
                if(fields.size()<3||!Int(fields[0],id)||id<0||id>1000000||fields[1].empty()){
                    ++status.rejected;continue;
                }
                if(definitions.size()>=250000){++status.rejected;break;}
                const auto name=Lower(fields[1]);
                definitions[name]={id,Classify(name,path.string(),section=="tobj"),
                                   Lower(fields[2])};
                const auto existing=idNames.find(id);
                if(existing==idNames.end())idNames.emplace(id,name);
                else if(existing->second!=name)existing->second.clear(); // ambiguous ID
                nameIds[name]=id;
                ++status.ideDefinitions;
            }else{
                if(section!="inst")continue;
                auto fields=Split(line);
                // text IPL: id,name,interior,x,y,z,rx,ry,rz,rw,lod (11 columns).
                // Quaternions and LOD relations remain metadata-only.
                int32_t id=-1,area=0;float x=0,y=0,z=0,quat=0;
                if(fields.size()<11||!Int(fields[0],id)||!Int(fields[2],area)||
                   area<INT16_MIN||area>INT16_MAX ||
                   !Float(fields[3],x)||!Float(fields[4],y)||!Float(fields[5],z)||
                   !Float(fields[6],quat)||!Float(fields[7],quat)||
                   !Float(fields[8],quat)||!Float(fields[9],quat) ||
                   fields[1].empty()){
                    ++status.rejected;continue;
                }
                int32_t cellX,cellY;
                if(!FitsCell(x,cellUnits,cellX)||!FitsCell(y,cellUnits,cellY)){
                    ++status.rejected;continue;
                }
                if(catalog.placements_.size()>=kMaxTotalPlacements){++status.rejected;break;}
                auto key=Lower(fields[1]);
                auto def=definitions.find(key);
                Placement placement{};
                // Unsupported Mod Loader precedence: IDs are untrusted hints.
                // An IPL ID must also match its IDE definition. Mod Loader
                // overrides will still require live name/ID resolution.
                const bool matches=def!=definitions.end() && def->second.id==id &&
                    CopyName(key,placement.modelName);
                placement.modelId=matches?id:-1;
                placement.category=matches?def->second.category:Category::Unknown;
                placement.x=x;placement.y=y;placement.z=z;
                placement.interior=int16_t(area);
                placement.cellX=cellX;placement.cellY=cellY;
                placement.identity=Mix(Mix(kBasis,Lower(path.string())),std::to_string(lineNo));
                const auto ordinal=uint32_t(catalog.placements_.size());
                catalog.placements_.push_back(placement);
                catalog.cells_[CellKey(cellX,cellY)].push_back(ordinal);
                if(!matches)++status.unresolved;
                else ++status.resolved;
            }
        }
        if(!file.eof())++status.rejected;
    };
    for(const auto& path:ideFiles)process(path,true);
    for(const auto& path:iplFiles)process(path,false);
    std::vector<BinaryJob> jobs;
    std::unordered_set<std::string> indexedArchives;
    auto indexArchive=[&](const std::filesystem::path& path){
        if(indexedArchives.insert(Lower(path.string())).second)
            IndexImg(path,jobs,catalog.assetCandidates_,
                     status.archiveAssetCandidates,status.manifestHash,
                     status.rejected,status.imgArchives);
    };
    for(const auto& archive:imgFiles)indexArchive(archive);
    std::error_code archiveEc;
    const auto models=root/"models";
    if(std::filesystem::is_directory(models,archiveEc) && !archiveEc){
        std::filesystem::directory_iterator it(models,
            std::filesystem::directory_options::skip_permission_denied,archiveEc);
        const std::filesystem::directory_iterator end;
        size_t visited=0;
        for(;!archiveEc && it!=end && visited++<1024;it.increment(archiveEc)){
            std::error_code ec;
            if(it->is_symlink(ec)||!it->is_regular_file(ec)||
               Lower(it->path().extension().string())!=".img")continue;
            auto canonical=std::filesystem::weakly_canonical(it->path(),ec);
            if(ec||!UnderRoot(root,canonical))continue;
            indexArchive(canonical);
        }
    }
    for(const auto& job:jobs){
        if(catalog.placements_.size()>=kMaxTotalPlacements){++status.rejected;break;}
        std::ifstream in(job.archive,std::ios::binary);
        if(!in || job.offset>uint64_t(std::numeric_limits<std::streamoff>::max())){
            ++status.unsupported;continue;
        }
        in.seekg(std::streamoff(job.offset));
        unsigned char header[76]{};
        if(!in.read(reinterpret_cast<char*>(header),76) ||
           std::memcmp(header,"bnry",4)!=0)continue; // other IPL section type
        const uint32_t count=U32(header+4);
        const uint32_t offset=U32(header+28);
        if(count>500000 || offset<76 || offset>job.length ||
           uint64_t(count)*40ull>job.length-offset ||
           uint64_t(count)>kMaxTotalPlacements-catalog.placements_.size()){
            ++status.unsupported;continue;
        }
        // Read only the bounded instance portion, not an entire IMG entry.
        std::vector<unsigned char> data(size_t(offset+uint64_t(count)*40ull),0);
        in.seekg(std::streamoff(job.offset));
        if(!in.read(reinterpret_cast<char*>(data.data()),
                    std::streamsize(data.size())) ||
           !BinaryPlacements(data,Lower(job.archive.string())+":"+job.name,
               cellUnits,idNames,nameIds,catalog.placements_,catalog.cells_,status)){
            ++status.unsupported;continue;
        }
        status.manifestHash=Mix(status.manifestHash,Lower(job.archive.string()));
        status.manifestHash=Mix(status.manifestHash,job.name);
        for(const auto c:data){status.manifestHash^=c;status.manifestHash*=1099511628211ull;}
    }
    // A filename match alone is NOT evidence that Mod Loader selected this
    // archive entry. Keep all candidates and make ambiguity observable.
    for(const auto& [filename,entries]:catalog.assetCandidates_){
        if(entries.size()>1)++status.ambiguousAssetNames;
    }
    for(const auto& [name,definition]:definitions){
        ModelDependency d{};
        d.id=definition.id;
        d.modelName=name;
        d.txdName=definition.txd;
        if(const auto it=catalog.assetCandidates_.find(name+".dff");
           it!=catalog.assetCandidates_.end())
            d.dffCandidates=uint32_t(it->second.size());
        if(const auto it=catalog.assetCandidates_.find(definition.txd+".txd");
           it!=catalog.assetCandidates_.end())
            d.txdCandidates=uint32_t(it->second.size());
        if(!definition.txd.empty())++status.modelDependencyEdges;
        if(d.dffCandidates || d.txdCandidates)
            ++status.modelsWithArchiveCandidates;
        catalog.dependencies_.emplace(name,std::move(d));
    }
    status.placements=catalog.placements_.size();
    status.cells=catalog.cells_.size();
    status.ready=true;
    status.complete=false; // explicit: unresolved binary IPL/Mod Loader possible.
    status.generation=status.manifestHash ? status.manifestHash:1;
    return catalog;
}
Hints Catalog::Query(int32_t cellX,int32_t cellY,size_t maxModels,
                     size_t* placementCursor) const {
    Hints out;
    auto it=cells_.find(CellKey(cellX,cellY));
    if(it==cells_.end())return out;
    out.placements=uint32_t(it->second.size());
    maxModels=std::min(maxModels,size_t(64));
    if(it->second.empty() || !maxModels)return out;
    size_t cursor=placementCursor?*placementCursor%it->second.size():0;
    std::unordered_set<int32_t> seen;
    for(size_t visited=0;visited<it->second.size();++visited){
        if(out.modelIds.size()>=maxModels)break;
        const auto ordinal=it->second[cursor];
        cursor=(cursor+1)%it->second.size();
        const auto id=placements_[ordinal].modelId;
        if(id>=0 && placements_[ordinal].modelName[0] &&
           seen.insert(id).second){
            out.modelIds.push_back(id);
            out.modelNames.push_back(placements_[ordinal].modelName);
            std::array<char,24> txd{};
            const std::string model(placements_[ordinal].modelName.data());
            if(const auto dependency=Dependency(model)){
                if(dependency->id==id && dependency->txdName.size()<txd.size())
                    std::copy(dependency->txdName.begin(),
                              dependency->txdName.end(),txd.begin());
            }
            out.txdNames.push_back(txd);
        }
    }
    if(placementCursor)*placementCursor=cursor;
    return out;
}
} // namespace asc::world_catalog


namespace asc::world_catalog {
const std::vector<ArchiveCandidate>* Catalog::CandidateAssets(
    const std::string& filename) const noexcept {
    const auto it=assetCandidates_.find(filename);
    return it==assetCandidates_.end()?nullptr:&it->second;
}
const ModelDependency* Catalog::Dependency(
    const std::string& modelName) const noexcept {
    const auto it=dependencies_.find(modelName);
    return it==dependencies_.end()?nullptr:&it->second;
}
} // namespace asc::world_catalog
