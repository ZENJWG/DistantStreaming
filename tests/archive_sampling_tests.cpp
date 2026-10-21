#include "asc/ArchiveSampling.h"
#include <iostream>
#include <stdexcept>
using namespace asc::archive;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
int main(){
    try{
        CHECK(ValidSampleRate(1)&&ValidSampleRate(16)&&ValidSampleRate(1024));
        CHECK(!ValidSampleRate(0)&&!ValidSampleRate(3)&&!ValidSampleRate(2048));
        for(unsigned rate:{1u,2u,16u,64u,1024u}){
            unsigned selected=0;
            for(unsigned i=1;i<=rate*4;++i)
                if(ShouldSample(i,rate))++selected;
            CHECK(selected==4);
        }
        CHECK(!ShouldSample(1,0));
        CHECK(LatencyBin(0)==0);
        CHECK(LatencyBin(24)==0&&LatencyBin(25)==1);
        CHECK(LatencyBin(49)==1&&LatencyBin(50)==2);
        CHECK(LatencyBin(99)==2&&LatencyBin(100)==3);
        CHECK(LatencyBin(249)==3&&LatencyBin(250)==4);
        CHECK(LatencyBin(499)==4&&LatencyBin(500)==5);
        CHECK(LatencyBin(999)==5&&LatencyBin(1000)==6);
        CHECK(LatencyBin(2499)==6&&LatencyBin(2500)==7);
        std::cout<<"PASS: archive sampler boundaries and counts\\n";
    } catch(const std::exception& e) {
        std::cerr<<"FAIL: "<<e.what()<<"\\n";return 1;
    }
}
