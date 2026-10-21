#include "asc/WorldCatalog.h"
// Portable, non-authoritative disk snapshots of the x64 helper's read-only catalog.
// The game must still validate model name+ID against live OLA/Mod Loader state.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_set>

namespace asc::world_catalog {
namespace {
constexpr uint64_t kFnv=14695981039346656037ull;
constexpr uint64_t kMaxFingerprintIO=512ull*1024*1024; // bounded scanner
constexpr uint64_t kMaxSnapshot=192ull*1024*1024;
constexpr uint64_t kMaxTextSource=64ull*1024*1024;
constexpr uint64_t kMaxImgSource=64ull*1024*1024*1024;
constexpr uint32_t kMaxPlacements=1500000, kMaxCandidates=200000, kMaxDependencies=250000;
constexpr uint32_t kVersion=1;
constexpr char kMagic[8]={'D','S','W','C','A','T','0','4'};
void HashBytes(uint64_t& hash,const void* p,size_t n){
    const auto* b=static_cast<const uint8_t*>(p);
    for(size_t i=0;i<n;++i){hash^=b[i];hash*=1099511628211ull;}
}
void HashString(uint64_t& hash,std::string_view s){
    uint64_t length=s.size();
    HashBytes(hash,&length,sizeof(length));
    HashBytes(hash,s.data(),s.size());
}
std::string Trim(std::string s){
    const auto start=s.find_first_not_of(" \t\r\n");
    if(start==std::string::npos)return {};
    const auto end=s.find_last_not_of(" \t\r\n");
    return s.substr(start,end-start+1);
}
std::string Lower(std::string s){
    for(char& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool Under(const std::filesystem::path& root,const std::filesystem::path& child){
    const auto a=root.lexically_normal(),b=child.lexically_normal();
    auto it=b.begin();
    for(const auto& part:a){
        if(it==b.end() || Lower(it->string())!=Lower(part.string()))return false;
        ++it;
    }
    for(;it!=b.end();++it)if(*it=="..")return false;
    return true;
}
std::optional<std::filesystem::path> SafePath(const std::filesystem::path& root,
                                              std::string relative,uint64_t limit){
    if(relative.empty()||relative.size()>512)return std::nullopt;
    std::replace(relative.begin(),relative.end(),'\\','/');
    std::filesystem::path part(relative);
    if(part.is_absolute() || part.has_root_name())return std::nullopt;
    for(const auto& x:part)if(x=="..")return std::nullopt;
    std::error_code ec;
    const auto full=std::filesystem::weakly_canonical(root/part,ec);
    if(ec||!Under(root,full)||!std::filesystem::is_regular_file(full,ec)||ec)return std::nullopt;
    const auto n=std::filesystem::file_size(full,ec);
    if(ec||n>limit)return std::nullopt;
    return full;
}
uint32_t Read32(const unsigned char* p){
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
uint16_t Read16(const unsigned char* p){
    return uint16_t(p[0]) | uint16_t(p[1])<<8;
}
bool HashRange(std::ifstream& input,uint64_t offset,uint64_t size,
               uint64_t& hash,uint64_t& budget){
    if(size>budget||offset>uint64_t(std::numeric_limits<std::streamoff>::max()))return false;
    input.clear();input.seekg(std::streamoff(offset));
    if(!input)return false;
    std::array<char,65536> buffer{};
    for(uint64_t done=0;done<size;){
        const auto n=size_t(std::min<uint64_t>(buffer.size(),size-done));
        if(!input.read(buffer.data(),std::streamsize(n)))return false;
        HashBytes(hash,buffer.data(),n);
        done+=n;
    }
    budget-=size;
    return true;
}
bool SourceHash(const std::filesystem::path& path,bool img,uint64_t& hash,uint64_t& budget){
    std::error_code ec;
    const auto size=std::filesystem::file_size(path,ec);
    if(ec||size>(img?kMaxImgSource:kMaxTextSource))return false;
    const auto stamp=std::filesystem::last_write_time(path,ec);
    if(ec)return false;
    HashString(hash,Lower(path.string()));
    HashBytes(hash,&size,sizeof(size));
    // Filesystem clock representation is used only within the same local
    // cache runtime/platform. Snapshot versions are platform-specific.
    const auto tick=stamp.time_since_epoch().count();
    HashBytes(hash,&tick,sizeof(tick));
    std::ifstream file(path,std::ios::binary);
    if(!file)return false;
    if(!img)return HashRange(file,0,size,hash,budget);
    unsigned char header[8]{};
    if(size<8||!file.read(reinterpret_cast<char*>(header),8))return false;
    HashBytes(hash,header,8);
    if(std::memcmp(header,"VER2",4)!=0)return true; // unsupported IMG unchanged
    const uint32_t count=Read32(header+4);
    if(count>150000||8ull+uint64_t(count)*32ull>size)return false;
    // Same-size directory edits invalidate even if the timestamp is restored.
    const uint64_t directorySize=uint64_t(count)*32ull;
    if(!HashRange(file,8,directorySize,hash,budget))return false;
    // IPL placements live inside archives; hash their bounded source bytes
    // rather than accepting same-size, same-timestamp binary IPL rewrites.
    file.clear();file.seekg(8);
    for(uint32_t i=0;i<count;++i){
        unsigned char entry[32]{};
        if(!file.read(reinterpret_cast<char*>(entry),32))return false;
        const auto* name=reinterpret_cast<const char*>(entry+8);
        const auto* end=static_cast<const char*>(std::memchr(name,0,24));
        if(!end)end=name+24;
        auto low=Lower(std::string(name,end));
        if(!low.ends_with(".ipl"))continue;
        const uint64_t offset=uint64_t(Read32(entry))*2048ull;
        const uint64_t sectors=Read16(entry+6)?Read16(entry+6):Read16(entry+4);
        const uint64_t bytes=sectors*2048ull;
        if(!bytes||bytes>kMaxTextSource||offset>size||bytes>size-offset)return false;
        const auto next=8ull+(uint64_t(i)+1)*32ull;
        if(!HashRange(file,offset,bytes,hash,budget))return false;
        file.clear();file.seekg(std::streamoff(next));
    }
    return true;
}
std::optional<uint64_t> Fingerprint(const std::filesystem::path& gameRoot,
                                    int32_t cellUnits){
    if(cellUnits<16||cellUnits>4096)return std::nullopt;
    std::error_code ec;
    const auto root=std::filesystem::weakly_canonical(gameRoot,ec);
    if(ec)return std::nullopt;
    const auto dat=SafePath(root,"data/gta.dat",kMaxTextSource);
    if(!dat)return std::nullopt;
    uint64_t hash=kFnv,budget=kMaxFingerprintIO;
    HashString(hash,Lower(root.string()));
    HashBytes(hash,&cellUnits,sizeof(cellUnits));
    if(!SourceHash(*dat,false,hash,budget))return std::nullopt;
    std::ifstream manifest(*dat);
    if(!manifest)return std::nullopt;
    std::set<std::string> textPaths,imgPaths;
    std::string line;size_t lines=0;
    while(std::getline(manifest,line)&&++lines<=8192){
        if(lines==1&&line.rfind("\xEF\xBB\xBF",0)==0)line.erase(0,3);
        if(line.size()>4096)continue;
        auto comment=line.find_first_of("#;");
        if(comment!=std::string::npos)line.erase(comment);
        line=Trim(line);
        auto split=line.find_first_of(" \t");
        if(split==std::string::npos)continue;
        const auto type=Lower(line.substr(0,split));
        if(type!="ide"&&type!="ipl"&&type!="img")continue;
        const auto relative=Trim(line.substr(split+1));
        HashString(hash,Lower(relative));
        auto path=SafePath(root,relative,type=="img"?kMaxImgSource:kMaxTextSource);
        if(path)(type=="img"?imgPaths:textPaths).insert(path->string());
        else HashString(hash,"UNRESOLVED");
    }
    if(!manifest.eof())return std::nullopt;
    const auto models=root/"models";
    if(std::filesystem::is_directory(models,ec)&&!ec){
        std::filesystem::directory_iterator it(models,
            std::filesystem::directory_options::skip_permission_denied,ec);
        const std::filesystem::directory_iterator end;
        size_t checked=0;
        for(;!ec&&it!=end&&checked++<1024;it.increment(ec)){
            std::error_code local;
            if(it->is_symlink(local)||!it->is_regular_file(local)||
               Lower(it->path().extension().string())!=".img")continue;
            const auto file=std::filesystem::weakly_canonical(it->path(),local);
            if(!local&&Under(root,file))imgPaths.insert(file.string());
        }
        if(ec)return std::nullopt;
    }
    for(const auto& file:textPaths)
        if(!SourceHash(file,false,hash,budget))return std::nullopt;
    for(const auto& file:imgPaths)
        if(!SourceHash(file,true,hash,budget))return std::nullopt;
    return hash;
}
struct Writer {
    std::vector<uint8_t> data;
    bool good=true;
    void U8(uint8_t v){
        if(!good)return;
        if(data.size()>=kMaxSnapshot){good=false;return;}
        data.push_back(v);
    }
    void U32(uint32_t v){for(int i=0;i<4;++i)U8(uint8_t(v>>(8*i)));}
    void U64(uint64_t v){for(int i=0;i<8;++i)U8(uint8_t(v>>(8*i)));}
    void I32(int32_t v){U32(uint32_t(v));}
    void F32(float v){U32(std::bit_cast<uint32_t>(v));}
    void String(const std::string& v,uint32_t cap){
        if(!good)return;
        if(v.size()>cap||data.size()>kMaxSnapshot-4||
           v.size()>kMaxSnapshot-4-data.size()){good=false;return;}
        U32(uint32_t(v.size()));
        if(good)data.insert(data.end(),v.begin(),v.end());
    }
};
struct Reader {
    const std::vector<uint8_t>& data;
    size_t at=0;bool good=true;
    uint8_t U8(){
        if(at>=data.size()){good=false;return 0;}
        return data[at++];
    }
    uint32_t U32(){
        uint32_t result=0;
        for(int i=0;i<4;++i)result|=uint32_t(U8())<<(8*i);
        return result;
    }
    uint64_t U64(){
        uint64_t result=0;
        for(int i=0;i<8;++i)result|=uint64_t(U8())<<(8*i);
        return result;
    }
    int32_t I32(){return std::bit_cast<int32_t>(U32());}
    float F32(){return std::bit_cast<float>(U32());}
    std::string String(uint32_t cap){
        const auto n=U32();
        if(!good||n>cap||n>data.size()-at){good=false;return {};}
        std::string s(reinterpret_cast<const char*>(data.data()+at),n);
        at+=n;return s;
    }
};
std::array<uint64_t,18> PackStatus(const Status& s){
    return {s.generation,s.manifestHash,s.sources,s.ideDefinitions,s.placements,
        s.resolved,s.unresolved,s.unsupported,s.rejected,s.cells,s.binarySources,
        s.binaryPlacements,s.imgArchives,s.ambiguousModels,s.archiveAssetCandidates,
        s.ambiguousAssetNames,s.modelDependencyEdges,s.modelsWithArchiveCandidates};
}
void UnpackStatus(Status& s,const std::array<uint64_t,18>& v){
    s.generation=v[0];s.manifestHash=v[1];s.sources=v[2];
    s.ideDefinitions=v[3];s.placements=v[4];s.resolved=v[5];
    s.unresolved=v[6];s.unsupported=v[7];s.rejected=v[8];
    s.cells=v[9];s.binarySources=v[10];s.binaryPlacements=v[11];
    s.imgArchives=v[12];s.ambiguousModels=v[13];
    s.archiveAssetCandidates=v[14];s.ambiguousAssetNames=v[15];
    s.modelDependencyEdges=v[16];s.modelsWithArchiveCandidates=v[17];
}
bool SafeName(const std::array<char,24>& name){
    auto end=std::find(name.begin(),name.end(),char(0));
    if(end==name.end())return false;
    for(auto it=name.begin();it!=end;++it){
        const unsigned char c=static_cast<unsigned char>(*it);
        if(!(std::isalnum(c)||c=='_'))return false;
    }
    for(auto it=end;it!=name.end();++it)if(*it)return false;
    return true;
}
bool NoNull(const std::string& v){return v.find('\0')==std::string::npos;}
bool ValidCandidatePath(const std::filesystem::path& root,const std::string& path){
    if(path.empty()||path.size()>4096||!NoNull(path))return false;
    return Under(root,std::filesystem::path(path));
}
} // namespace

Catalog Catalog::LoadOrBuild(const std::filesystem::path& gameRoot,
                             const std::filesystem::path& cacheFile,
                             int32_t cellUnits){
    const auto fp=Fingerprint(gameRoot,cellUnits);
    std::ifstream in;
    auto build=[&] {
        // A rejected snapshot must release its Windows read handle before
        // replacement. Otherwise rename/remove fails and every start rebuilds.
        if(in.is_open())in.close();
        Catalog result=Build(gameRoot,cellUnits);
        if(!fp || !result.status_.ready)return result;
        // Changes mid-build must never become trusted persisted data.
        const auto after=Fingerprint(gameRoot,cellUnits);
        if(!after||*after!=*fp)return result;
        try {
            Writer w;
            for(const auto x:PackStatus(result.status_))w.U64(x);
            if(result.placements_.size()>kMaxPlacements ||
               result.assetCandidates_.size()>kMaxCandidates ||
               result.dependencies_.size()>kMaxDependencies)return result;
            w.U32(uint32_t(result.placements_.size()));
            for(const auto& p:result.placements_){
                w.U64(p.identity);w.I32(p.modelId);
                for(const auto b:p.modelName)w.U8(uint8_t(b));
                w.F32(p.x);w.F32(p.y);w.F32(p.z);
                w.U32(uint16_t(p.interior));w.U8(uint8_t(p.category));
                w.I32(p.cellX);w.I32(p.cellY);
            }
            w.U32(uint32_t(result.assetCandidates_.size()));
            // Stable disk representation independent of unordered_map layout.
            std::vector<std::string> names;
            for(const auto& [name,_]:result.assetCandidates_)names.push_back(name);
            std::sort(names.begin(),names.end());
            for(const auto& name:names){
                const auto& candidates=result.assetCandidates_.at(name);
                w.String(name,64);w.U32(uint32_t(candidates.size()));
                for(const auto& c:candidates){
                    w.String(c.archive,4096);
                    w.U64(c.offset);w.U64(c.bytes);w.U8(uint8_t(c.type));
                }
            }
            names.clear();
            for(const auto& [name,_]:result.dependencies_)names.push_back(name);
            std::sort(names.begin(),names.end());
            w.U32(uint32_t(names.size()));
            for(const auto& name:names){
                const auto& d=result.dependencies_.at(name);
                w.String(name,128);w.I32(d.id);w.String(d.txdName,128);
                w.U32(d.dffCandidates);w.U32(d.txdCandidates);
            }
            if(!w.good||w.data.size()>kMaxSnapshot)return result;
            const auto parentDir=cacheFile.parent_path();
            if(parentDir.empty())return result;
            std::error_code ec;
            std::filesystem::create_directories(parentDir,ec);
            if(ec)return result;
            auto temp=cacheFile;
            temp+=std::string(".tmp.")+
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            {
                std::ofstream out(temp,std::ios::binary|std::ios::trunc);
                if(!out)return result;
                const auto payloadSize=uint64_t(w.data.size());
                uint64_t checksum=kFnv;
                HashBytes(checksum,w.data.data(),w.data.size());
                Writer hdr;
                for(char c:kMagic)hdr.U8(uint8_t(c));
                hdr.U32(kVersion);hdr.U32(0x01020304);
                hdr.I32(cellUnits);hdr.U64(*fp);
                hdr.U64(payloadSize);hdr.U64(checksum);
                out.write(reinterpret_cast<const char*>(hdr.data.data()),
                          std::streamsize(hdr.data.size()));
                out.write(reinterpret_cast<const char*>(w.data.data()),
                          std::streamsize(w.data.size()));
                out.flush();
                if(!out.good()){out.close();std::filesystem::remove(temp,ec);return result;}
            }
            // Windows cannot rename over an existing destination. Readers
            // reject a missing or incomplete file and build from the sources.
            std::filesystem::rename(temp,cacheFile,ec);
            if(ec){
                std::filesystem::remove(cacheFile,ec);
                ec.clear();std::filesystem::rename(temp,cacheFile,ec);
            }
            if(ec)std::filesystem::remove(temp,ec);
        }catch(...){} // optional cache cannot break native streaming
        return result;
    };
    if(!fp)return build();
    try {
        std::error_code ec;
        const auto n=std::filesystem::file_size(cacheFile,ec);
        if(ec||n<44||n>kMaxSnapshot+44)return build();
        in.open(cacheFile,std::ios::binary);
        if(!in)return build();
        std::vector<uint8_t> header(44);
        if(!in.read(reinterpret_cast<char*>(header.data()),44))return build();
        Reader r{header};
        for(const char c:kMagic)if(r.U8()!=uint8_t(c))return build();
        const auto version=r.U32(),endian=r.U32();
        const auto units=r.I32();
        const auto savedFp=r.U64(),bytes=r.U64(),checksum=r.U64();
        if(!r.good||version!=kVersion||endian!=0x01020304||units!=cellUnits||
           savedFp!=*fp||bytes!=n-44||bytes>kMaxSnapshot)return build();
        std::vector<uint8_t> content(size_t(bytes),uint8_t(0));
        if(!in.read(reinterpret_cast<char*>(content.data()),std::streamsize(bytes)))
            return build();
        uint64_t actual=kFnv;HashBytes(actual,content.data(),content.size());
        if(actual!=checksum)return build();
        Reader d{content};
        Catalog result;
        std::array<uint64_t,18> counts{};
        for(auto& x:counts)x=d.U64();
        UnpackStatus(result.status_,counts);
        if(result.status_.placements>kMaxPlacements||
           result.status_.archiveAssetCandidates>kMaxCandidates||
           result.status_.ideDefinitions>kMaxDependencies||
           !result.status_.generation)return build();
        const auto np=d.U32();
        if(np>kMaxPlacements||np!=result.status_.placements)return build();
        result.placements_.reserve(np);
        for(uint32_t i=0;i<np&&d.good;++i){
            Placement p{};
            p.identity=d.U64();p.modelId=d.I32();
            for(auto& b:p.modelName)b=char(d.U8());
            p.x=d.F32();p.y=d.F32();p.z=d.F32();
            p.interior=int16_t(d.U32());p.category=Category(d.U8());
            p.cellX=d.I32();p.cellY=d.I32();
            if(!d.good||!std::isfinite(p.x)||!std::isfinite(p.y)||
               !std::isfinite(p.z)||p.modelId< -1||p.modelId>1000000||
               uint8_t(p.category)>uint8_t(Category::Lod)||!SafeName(p.modelName)||
               (p.modelId==-1&&p.modelName[0])||
               (p.modelId>=0&&!p.modelName[0]))return build();
            const auto x=std::floor(double(p.x)/cellUnits);
            const auto y=std::floor(double(p.y)/cellUnits);
            if(x!=p.cellX||y!=p.cellY)return build();
            result.cells_[CellKey(p.cellX,p.cellY)].push_back(i);
            result.placements_.push_back(p);
        }
        const auto ng=d.U32();
        if(!d.good||ng>kMaxCandidates)return build();
        uint64_t total=0;
        std::error_code ecRoot;
        const auto canonicalRoot=std::filesystem::weakly_canonical(gameRoot,ecRoot);
        if(ecRoot)return build();
        for(uint32_t i=0;i<ng&&d.good;++i){
            auto name=d.String(64);
            const auto nc=d.U32();
            if(name.empty()||!NoNull(name)||nc>kMaxCandidates-total||
               result.assetCandidates_.contains(name))return build();
            std::vector<ArchiveCandidate> candidates;
            candidates.reserve(nc);
            for(uint32_t j=0;j<nc&&d.good;++j){
                ArchiveCandidate c;
                c.archive=d.String(4096);c.offset=d.U64();c.bytes=d.U64();
                c.type=AssetType(d.U8());
                if(!ValidCandidatePath(canonicalRoot,c.archive)||c.bytes==0||
                   c.offset>kMaxImgSource||c.bytes>kMaxTextSource||
                   c.bytes>kMaxImgSource-c.offset||
                   uint8_t(c.type)<1||uint8_t(c.type)>3)return build();
                candidates.push_back(std::move(c));
            }
            total+=nc;
            result.assetCandidates_.emplace(std::move(name),std::move(candidates));
        }
        const auto nd=d.U32();
        if(!d.good||nd>kMaxDependencies)return build();
        for(uint32_t i=0;i<nd&&d.good;++i){
            ModelDependency dep;
            dep.modelName=d.String(128);dep.id=d.I32();
            dep.txdName=d.String(128);
            dep.dffCandidates=d.U32();dep.txdCandidates=d.U32();
            if(dep.modelName.empty()||!NoNull(dep.modelName)||!NoNull(dep.txdName)||
               dep.id<0||dep.id>1000000||
               dep.dffCandidates>kMaxCandidates||dep.txdCandidates>kMaxCandidates||
               result.dependencies_.contains(dep.modelName))return build();
            result.dependencies_.emplace(dep.modelName,std::move(dep));
        }
        if(!d.good||d.at!=d.data.size()||
           total!=result.status_.archiveAssetCandidates||
           result.cells_.size()!=result.status_.cells)return build();
        // Recheck after deserializing: contents may have changed during I/O.
        const auto after=Fingerprint(gameRoot,cellUnits);
        if(!after||*after!=*fp)return build();
        result.status_.ready=true;result.status_.complete=false;
        result.status_.loadedFromCache=true;
        return result;
    }catch(...){return build();}
}
} // namespace asc::world_catalog
