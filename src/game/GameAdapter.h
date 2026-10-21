#pragma once
#include "asc/Core.h"
#include "asc/OffscreenCleanup.h"
#include <array>
namespace asc::motion {class RequestBudget;}

class GameAdapter final : public asc::IGame {
public:
    void SetRequestBudget(asc::motion::RequestBudget* budget){requestBudget_=budget;}
    void SetMotion(bool travel,bool camera){fastTravel_=travel;fastCamera_=camera;}
    unsigned RequestAllowance() const override;
    void Configure(const asc::Config& cfg,asc::offscreen::Settings cleanup={}){
        priorityRadius_=cfg.priorityRadiusUnits;
        cleanup_=cleanup;
        scored_.reserve(4096);
    }
    std::vector<asc::Candidate> Collect(
        const std::vector<asc::Cell>& cells,
        asc::Vec3 current, asc::Vec3 future,
        asc::Vec3 camera, asc::Vec3 forward,
        bool cameraValid, unsigned maxCandidates) override;
    bool CollectInto(const std::vector<asc::Cell>& cells,
        asc::Vec3 current, asc::Vec3 future, asc::Vec3 camera,
        asc::Vec3 forward, bool cameraValid, unsigned maxCandidates,
        std::vector<asc::Candidate>& out) override;
    bool CanRequest(int id) const override;
    bool IsLoaded(int id) const override;
    bool IsStillRequested(int id) const override;
    void RequestDefault(int id) override;
    void RequestPriority(int id) override;
private:
    void Request(int id,bool priority);
    float priorityRadius_=100.0f;
    asc::motion::RequestBudget* requestBudget_=nullptr;
    bool fastTravel_=false,fastCamera_=false;
    std::vector<asc::Candidate> scored_;
    std::array<int,asc::kVanillaModelInfoCountCap> scoredIndex_{};
    asc::offscreen::Settings cleanup_{};
};
