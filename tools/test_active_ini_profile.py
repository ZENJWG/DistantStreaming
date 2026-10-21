#!/usr/bin/env python3
"""Validate shipped aggressive OLA INI against engine-side configured bounds."""
from configparser import ConfigParser
from pathlib import Path

root=Path(__file__).resolve().parent.parent
config=ConfigParser(strict=True, inline_comment_prefixes=(";",))
assert config.read(root/"config/DistantStreaming.ini")
get=lambda section,key: config.getint(section,key)
assert get("General","Enabled")==1
assert get("Diagnostics","HookMode")==1
assert get("Diagnostics","ControllerEnabled")==1
assert get("Diagnostics","HookIntegrityMonitor")==1
assert get("Diagnostics","RecoverPristineHooks")==1
assert 250<=get("Diagnostics","HookCheckIntervalMs")<=10000
assert get("Memory","ManageStreamingMemory")==1
assert get("Memory","DistantStreamingOwnsNativeBudget")==1
assert get("Memory","StreamingMemoryLimitMiB")==0 or 64<=get("Memory","StreamingMemoryLimitMiB")<=4096
assert 64<=get("Memory","AddressSpaceReserveMiB")<=1024
assert 30<=get("Memory","ResumePercent")<get("Memory","ThrottlePercent")<get("Memory","HighPercent")<=95
assert 16<=get("General","UpdateIntervalMs")<=1000
assert 1<=get("Prefetch","MaxRequestsPerTick")<=16
assert 1<=get("Prefetch","MaxAdaptiveRequests")<=get("Prefetch","MaxRequestsPerTick")
assert get("Prefetch","MaxRequestsPerTick")<=get("Prefetch","MaxOutstanding")<=128
assert get("Prefetch","MaxRequestsPerTick")<=get("Prefetch","BacklogLimit")<=128
assert 1<=get("Prefetch","MaxPriorityRequestsPerTick")<=4
assert 2<=get("Prefetch","MaxSectorScans")<=128
assert get("Prefetch","MaxRequestsPerTick")<=get("Prefetch","MaxCandidates")<=512
assert 0<=get("Prefetch","LookAheadMs")<=4000
assert 25<=get("Prefetch","SectorRadiusUnits")<=200
assert 10<=get("Performance","SlowFrameMs")<=200
assert 5<=get("Performance","CautionFrameMs")<get("Performance","SlowFrameMs")
assert 100<=get("CameraReadiness","IntervalMs")<=5000
assert 1<=get("CameraReadiness","MaxModelsPerBatch")<=8
assert 0<=get("CameraReadiness","MaxInstancesPerBatch")<=64
assert 50<=get("CameraReadiness","RangeUnits")<=600
assert 32<=get("OffscreenCleanup","MaxScanPerFrame")<=4096
assert 1<=get("OffscreenCleanup","MaxDeletesPerFrame")<=32
assert get("OffscreenCleanup","MaxDeletesPerFrame")<=get("OffscreenCleanup","MaxPressureDeletesPerFrame")<=64
assert 100<=get("OffscreenCleanup","TimeBudgetUs")<=4000
assert 25<=get("OffscreenCleanup","KeepNearPlayerUnits")<=250
assert get("GPUResidency","Enabled")==1
assert 512<=get("GPUResidency","TargetMiB")<=32768
assert 128<=get("GPUResidency","BudgetHeadroomMiB")<=4096
assert 16<=get("ArchivePreload","CacheLimitMiB")<=1024
print("PASS: active OLA/GPU direct profile accepted; streaming profile accepted")
