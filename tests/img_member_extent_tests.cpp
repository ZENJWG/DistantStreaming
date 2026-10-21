#include "asc/ImgMemberExtent.h"
#include <cassert>
#include <cstring>
#include <vector>
using namespace asc::img_member;
static void u16(std::vector<uint8_t>& v,size_t n,uint16_t x){
    v[n]=uint8_t(x);v[n+1]=uint8_t(x>>8);
}
static void u32(std::vector<uint8_t>& v,size_t n,uint32_t x){
    for(int i=0;i<4;++i)v[n+i]=uint8_t(x>>(8*i));
}
int main(){
    std::vector<uint8_t> directory(8+2*32);
    std::memcpy(directory.data(),"VER2",4);u32(directory,4,2);
    u32(directory,8,1);u16(directory,8+6,2);
    std::memcpy(directory.data()+8+8,"tree01.dff",10);
    u32(directory,40,3);u16(directory,40+6,2);
    std::memcpy(directory.data()+40+8,"tree01.txd",10);
    size_t length=0;
    assert(DirectorySize(std::span(directory.data(),8),12288,length));
    assert(length==directory.size());
    std::vector<Extent> members;
    assert(Parse(directory,12288,members)&&members.size()==2);
    asc::native::CatalogCellReply hints{};
    hints.modelCount=1;
    std::memcpy(hints.modelNames[0],"tree01",7);
    std::memcpy(hints.txdNames[0],"tree01",7);
    Extent selected{};
    assert(FindObserved(members,2048,2048,hints,selected)==Decision::Match);
    assert(selected.offset==2048&&selected.bytes==4096);
    assert(FindObserved(members,6144,1024,hints,selected)==Decision::Match);
    assert(FindObserved(members,2048,4097,hints,selected)==Decision::Ambiguous);
    assert(FindObserved(members,0,2048,hints,selected)==Decision::NotSelected);
    assert(FindObserved(members,2048,0,hints,selected)==Decision::Invalid);
    hints.modelCount=0;
    assert(FindObserved(members,2048,2048,hints,selected)==Decision::Invalid);
    hints.modelCount=1;
    std::memcpy(hints.modelNames[0],"another",8);
    std::memcpy(hints.txdNames[0],"another",8);
    assert(FindObserved(members,2048,2048,hints,selected)==Decision::NotSelected);
    std::memcpy(hints.modelNames[0],"tree01",7);
    // Physical sector overlaps invalidate the whole archive directory.
    u32(directory,40,2);
    assert(!Parse(directory,12288,members));
    u32(directory,40,3);
    u32(directory,8,0xffffffffu);
    assert(!Parse(directory,12288,members));
    u32(directory,8,1);
    directory[8+8]='/';
    assert(!Parse(directory,12288,members));
    directory[8+8]='t';
    u32(directory,4,150001);
    assert(!DirectorySize(std::span(directory.data(),8),12288,length));
}
