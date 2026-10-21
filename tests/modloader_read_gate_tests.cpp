#include "asc/ModLoaderReadGate.h"
#include <cassert>
#include <cstdint>

int main() {
    using namespace asc::modloader_cache;
    assert(LooseSourcePath(LR"(\\?\C:\Games\SA\modloader\vehicle\infernus.dff)"));
    assert(LooseSourcePath(LR"(C:\GAMES\SA\MODLOADER\HD\TEXTURES.TXD)"));
    assert(LooseSourcePath(LR"(D:\scripts\modloader\level\stuff.col)"));
    assert(!LooseSourcePath(LR"(C:\Games\SA\models\gta3.img)"));
    assert(!LooseSourcePath(LR"(C:\Games\SA\modloader2\vehicle\infernus.dff)"));
    assert(!LooseSourcePath(LR"(C:\Games\SA\modloader\vehicles\foo.dff.bak)"));
    assert(!LooseSourcePath(LR"(C:\Games\SA\modloader\file.mp3)"));
    assert(ClassifySource(LR"(C:\Games\SA\models\gta3.img)") == SourceKind::Img);
    assert(ClassifySource(LR"(C:\Mods\my.img)") == SourceKind::Img);
    assert(ClassifySource(LR"(C:\Games\SA\modloader\hd\car.txd)") == SourceKind::Loose);
    assert(ClassifySource(LR"(C:\Games\SA\models\car.txd)") == SourceKind::Unsupported);
    assert(EligibleRead(SourceKind::Img,16384,4096,8192,8*1024*1024,true,true));
    assert(!EligibleRead(SourceKind::Img,16384,128,8192,8*1024*1024,true,true));
    assert(!EligibleRead(SourceKind::Img,16384,4096,8191,8*1024*1024,true,true));
    assert(!EligibleRead(SourceKind::Img,16384,12288,8192,8*1024*1024,true,true));
    assert(EligibleRead(SourceKind::Loose,2048,0,2048,8*1024*1024,true,true));
    assert(!EligibleRead(SourceKind::Loose,2048,256,512,8*1024*1024,true,true));
    assert(CompleteLooseRead(2048,0,2048,4*1024*1024,true,true));
    assert(!CompleteLooseRead(2048,2,2048,4*1024*1024,true,true));
    assert(!CompleteLooseRead(2048,0,1024,4*1024*1024,true,true));
    assert(!CompleteLooseRead(2048,0,2048,4*1024*1024,false,true));
    assert(!CompleteLooseRead(2048,0,2048,4*1024*1024,true,false));
    assert(!CompleteLooseRead(8*1024*1024,0,8*1024*1024,4*1024*1024,true,true));
    return 0;
}
