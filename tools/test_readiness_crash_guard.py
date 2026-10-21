#!/usr/bin/env python3
"""Portable source-regression check for GTA SA OnRequest+0x42 crash.
Checks the compiled Windows sources are gated before game-object traversal.
It is NOT a gameplay or MSVC runtime test.
"""
from pathlib import Path
root=Path(__file__).resolve().parent.parent
readiness=(root/"src/game/Readiness.cpp").read_text()
main=(root/"src/game/main.cpp").read_text()
conf=(root/"src/game/ConfigWin.cpp").read_text()
ini=(root/"config/DistantStreaming.ini").read_text()
def check(value,description):
    assert value, description

start=readiness.index("void Tracker::OnRequest(")
end=readiness.index("void Tracker::Sample(",start)
onrequest=readiness[start:end]
check("CStreaming::" not in onrequest and "CModelInfo::" not in onrequest
      and "ms_pArrayBase" not in onrequest and "TryGetLoadState" not in onrequest,
      "OnRequest must not read game-owned tables")
check("ReadProcessMemory(" in readiness and "CStreamingInfo::ms_pArrayBase" in readiness,
      "Opt-in sample must use safe-copy from live streaming array")
check("if(config.core.prefetch || config.core.dynamicWarmup || config.readinessTelemetry)" in main,
      "Request consumer must be explicitly gated")
check("if(self.config.readinessTelemetry)" in main,
      "tracker OnRequest must be opt-in")
check("if(config.readinessTelemetry)" in main,
      "tracker Sample must be opt-in")
bindings=(root/"src/game/OlaBindingsWin.cpp").read_text()
check("ola_bindings::Bind(log)" in main and "SAFETY STOP:" in main,
      "unresolved runtime pointers must disable controller")
check("CStreamingInfo::ms_pArrayBase" in bindings and
      "DecodeEntityModelTable" in bindings and
      "CModelInfo::ms_modelInfoPtrs=resolved" in bindings and
      "FAIL CLOSED" in bindings,
      "OLA rebinding must validate the live arrays and fail closed")
check('ini(p,"Diagnostics","ReadinessTelemetry",0)' in conf
      and "ReadinessTelemetry=0" in ini,
      "Telemetry unsafe path must default off")
check("HookMode=1" in ini and "ControllerEnabled=1" in ini and
      "ManageStreamingMemory=1" in ini and
      "DistantStreamingOwnsNativeBudget=1" in ini and
      "ReadinessTelemetry=0" in ini,
      "Active OLA profile must use Request hook and explicit ASC budget ownership")
hooks=(root/"src/game/Hooks.cpp").read_text()
check("CheckIntegrity(" in hooks and "Request hook replaced or lost" in hooks and
      "State::Pristine" in hooks and "State::Owned" in hooks,
      "Request hook must be checked and only pristine code re-armed")
check("hookIntegrityMonitor" in main and "RecoverPristineHooks" in main and
      "bindingAttempts>=12" in main,
      "Runtime must monitor own hook and retry OLA binding with a limit")
print("PASS: OnRequest isolation, OLA ownership checks and optimized active profile")
