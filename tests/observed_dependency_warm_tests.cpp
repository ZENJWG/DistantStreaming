#include "asc/ObservedDependencyWarmPolicy.h"
#include <cassert>
#include <cstring>
#include <string>
int main(){
    using namespace asc::dependency_warm;
    asc::native::CatalogCellReply hints{};
    assert(MatchLooseFile("tree.dff",hints)==Decision::NoHints);
    hints.modelCount=2;
    std::memcpy(hints.modelNames[0],"tree_01",8);
    std::memcpy(hints.txdNames[0],"veg_shared",11);
    std::memcpy(hints.modelNames[1],"bridge_02",10);
    std::memcpy(hints.txdNames[1],"building",9);
    assert(MatchLooseFile("TREE_01.DFF",hints)==Decision::Match);
    assert(MatchLooseFile("veg_shared.txd",hints)==Decision::Match);
    assert(MatchLooseFile("BUILDING.TXD",hints)==Decision::Match);
    assert(MatchLooseFile("bridge_02.DFF",hints)==Decision::Match);
    assert(MatchLooseFile("other.dff",hints)==Decision::NoMatch);
    assert(MatchLooseFile("TREE_01.img",hints)==Decision::Unsupported);
    assert(MatchLooseFile("../tree_01.dff",hints)==Decision::Unsupported);
    assert(MatchLooseFile("C:\\\\tree_01.dff",hints)==Decision::Unsupported);
    assert(MatchLooseFile("tree_01.dff/malicious",hints)==Decision::Unsupported);
    assert(MatchLooseFile("tree_01.txD\\0garbage",hints)==Decision::Unsupported);
    // Non-null-terminated 24-byte fields cannot match source filenames.
    std::memset(hints.modelNames[0],'a',24);
    assert(MatchLooseFile(std::string(24,'a')+".dff",hints)==Decision::NoMatch);
    hints.modelCount=65;
    assert(MatchLooseFile("bridge_02.dff",hints)==Decision::NoHints);
    static_assert(sizeof(asc::native::CatalogCellReply)==3344);
}
