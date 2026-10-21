#include "asc/RequestCallProfile.h"
#include <cassert>
#include <cstdint>

using asc::request_profile::Profiler;
using asc::request_profile::Sample;
using asc::stream_flow::State;

int main(){
    Profiler<2,2> p;
    p.Record({0x401234,11,100},State::Requested,true);
    p.Record({0x401234,11,110},State::Requested,true); // no latency reset
    p.Record({0x405678,12,120},State::Channeled,true);
    p.Record({0x409999,13,130},State::Requested,true); // bounded eviction / no tracking
    p.Record({0x401234,99999,130},State::Other,false);
    auto one=p.Take();
    assert(one.examined==5 && one.invalidIds==0 && one.stateUnavailable==1);
    assert(one.families[static_cast<unsigned>(asc::request_profile::ResourceFamily::Extension)]==1);
    assert(one.states[1]==3 && one.states[2]==1);
    assert(one.active==2 && one.trackCapacityMisses==1);
    assert(one.callers[0].count>=3);
    assert(one.models[0].id==11 && one.models[0].count==2);
    assert(one.models[0].states[1]==2);
    p.Poll(180,[](int id){ return id==11?State::Loaded:State::Requested;});
    auto two=p.Take();
    assert(two.completed==1 && two.elapsedTotalMs==80 && two.active==1);
    p.Poll(190,[](int){return State::NotLoaded;});
    auto three=p.Take();
    assert(three.cancelled==1 && three.active==0);
    p.Record({0x400001,17,UINT32_MAX-5},State::Finishing,true);
    p.Poll(10,[](int){return State::Loaded;});
    auto wrap=p.Take();
    assert(wrap.completed==1 && wrap.elapsedTotalMs==16);
    p.Record({0x400001,18,10},State::Requested,true);
    p.Poll(30011,[](int){return State::Requested;});
    assert(p.Take().timedOut==1);
    p.Record({0x400001,19,100},State::Loaded,true);
    assert(p.Take().active==0);
    p.Reset();
    assert(p.Take().examined==0);
    using asc::request_profile::Classify;
    using asc::request_profile::ResourceFamily;
    using asc::request_profile::AverageNanoseconds;
    assert(Classify(-1)==ResourceFamily::Negative);
    assert(Classify(0)==ResourceFamily::DFF && Classify(19999)==ResourceFamily::DFF);
    assert(Classify(20000)==ResourceFamily::TXD && Classify(24999)==ResourceFamily::TXD);
    assert(Classify(25000)==ResourceFamily::COL && Classify(25254)==ResourceFamily::COL);
    assert(Classify(25255)==ResourceFamily::IPL && Classify(25510)==ResourceFamily::IPL);
    assert(Classify(25511)==ResourceFamily::DAT && Classify(25574)==ResourceFamily::DAT);
    assert(Classify(25575)==ResourceFamily::IFP && Classify(25754)==ResourceFamily::IFP);
    assert(Classify(25755)==ResourceFamily::RRR && Classify(26229)==ResourceFamily::RRR);
    assert(Classify(26230)==ResourceFamily::SCM && Classify(26311)==ResourceFamily::SCM);
    assert(Classify(26312)==ResourceFamily::Internal && Classify(26315)==ResourceFamily::Internal);
    assert(Classify(26316)==ResourceFamily::Extension);
    assert(!asc::request_profile::StateIndexCandidate(26312));
    assert(asc::request_profile::StateIndexCandidate(26311));
    assert(AverageNanoseconds(20,10000000,2)==1000);
    assert(AverageNanoseconds(5,10000000)==500);
    assert(AverageNanoseconds(5,0)==0);
    p.Record({0x408915,20005,200},State::Loaded,true);
    p.Record({0x408915,25000,200},State::Requested,true);
    p.Record({0x408915,26312,200},State::Other,false);
    const auto classified=p.Take();
    assert(classified.examined==3 && classified.stateUnavailable==1);
    assert(classified.families[static_cast<unsigned>(ResourceFamily::TXD)]==1);
    assert(classified.families[static_cast<unsigned>(ResourceFamily::COL)]==1);
    assert(classified.families[static_cast<unsigned>(ResourceFamily::Internal)]==1);
    assert(classified.states[4]==1 && classified.states[1]==1);
}
