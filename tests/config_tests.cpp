#include "ConfigWin.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
int main(){
    const auto dir=std::filesystem::temp_directory_path()/
        ("asc-config-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount()));
    const auto file=dir/"DistantStreaming.ini";
    try {
        std::filesystem::create_directory(dir);
        auto defaults=ReadConfig(dir.string());
        CHECK(defaults.externalCache.helperBudgetMiB==8192);
        CHECK(!defaults.externalCache.serveCdStreamHits);
        CHECK(defaults.externalCache.discoverModLoaderArchives);
        CHECK(WritePrivateProfileStringA("ExternalAssetCache64","CacheLimitMiB",
                                        "65536",file.string().c_str()));
        CHECK(ReadConfig(dir.string()).externalCache.helperBudgetMiB==32768);
        CHECK(WritePrivateProfileStringA("ExternalAssetCache64","CacheLimitMiB",
                                        "32768",file.string().c_str()));
        CHECK(defaults.enabled&&!defaults.streamingMemory.enabled&&
              defaults.streamingMemory.limitMiB==512 &&
              !defaults.cameraReadiness.enabled&&!defaults.offscreenCleanup.enabled&&
              !defaults.core.prefetch && defaults.hookMode==0 &&
              !defaults.controllerEnabled && !defaults.readinessTelemetry);
        CHECK(WritePrivateProfileStringA("Diagnostics","HookMode","1",file.string().c_str()));
        CHECK(WritePrivateProfileStringA("Diagnostics","ControllerEnabled","1",file.string().c_str()));
        CHECK(ReadConfig(dir.string()).hookMode==1 && ReadConfig(dir.string()).controllerEnabled);
        CHECK(WritePrivateProfileStringA("Diagnostics","ReadinessTelemetry","1",file.string().c_str()));
        CHECK(ReadConfig(dir.string()).readinessTelemetry);
        CHECK(WritePrivateProfileStringA("Diagnostics","ReadinessTelemetry","0",file.string().c_str()));
        CHECK(!ReadConfig(dir.string()).readinessTelemetry);
        CHECK(WritePrivateProfileStringA("Diagnostics","HookMode","2",file.string().c_str()));
        CHECK(ReadConfig(dir.string()).hookMode==2);
        CHECK(WritePrivateProfileStringA("Diagnostics","HookMode","8",file.string().c_str()));
        CHECK(ReadConfig(dir.string()).hookMode==2); // invalid values are bounded
        CHECK(WritePrivateProfileStringA("OffscreenCleanup","Enabled","1",file.string().c_str()));
        CHECK(WritePrivateProfileStringA("CameraReadiness","BlockingLoad","1",file.string().c_str()));
        CHECK(WritePrivateProfileStringA("CameraReadiness","MaxInstancesPerBatch","32",file.string().c_str()));
        auto cfg=ReadConfig(dir.string());
        CHECK(cfg.enabled&&cfg.offscreenCleanup.enabled);
        CHECK(!cfg.cameraReadiness.blocking&&cfg.cameraReadiness.maxInstances==4);
        CHECK(WritePrivateProfileStringA("OffscreenCleanup","DisableBlockingLoads","0",file.string().c_str()));
        cfg=ReadConfig(dir.string());
        CHECK(cfg.enabled&&!cfg.cameraReadiness.blocking&&cfg.cameraReadiness.maxInstances==4);
        CHECK(WritePrivateProfileStringA("FrameStability","Enabled","0",file.string().c_str()));
        cfg=ReadConfig(dir.string());
        CHECK(cfg.enabled&&cfg.cameraReadiness.blocking&&cfg.cameraReadiness.maxInstances==32);
        CHECK(WritePrivateProfileStringA("OffscreenCleanup","TimeBudgetUs","-1",file.string().c_str()));
        CHECK(!ReadConfig(dir.string()).enabled);
        CHECK(WritePrivateProfileStringA("OffscreenCleanup","TimeBudgetUs","1000",file.string().c_str()));
        std::filesystem::remove(file);std::filesystem::remove(dir);
        std::cout<<"PASS: config clamping and controller isolation\n";
    }catch(const std::exception& e){
        std::filesystem::remove(file);std::filesystem::remove(dir);
        std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;
    }
}
