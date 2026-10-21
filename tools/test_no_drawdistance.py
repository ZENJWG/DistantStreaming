#!/usr/bin/env python3
"""Regression guard: Project2DFX owns visual distance and renderer extensions."""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
compiled=(
    root/"CMakeLists.txt",
    root/"src/game/main.cpp",
    root/"src/game/ConfigWin.cpp",
    root/"src/game/ConfigWin.h",
    root/"include/asc/RenderCostProbe.h",
)
for path in compiled:
    source=path.read_text(encoding="utf-8")
    for symbol in ("DrawDistanceWin", "drawDistance", "draw_distance::",
                   "DistanceTick", "CameraApply", "renderSceneEvent"):
        assert symbol not in source, f"{path.name}: legacy renderer symbol {symbol}"

for relative in ("src/game/DrawDistanceWin.cpp",
                 "src/game/DrawDistanceWin.h",
                 "include/asc/DrawDistancePolicy.h"):
    assert not (root/relative).exists(), f"obsolete renderer exists: {relative}"

for path in (root/"config").glob("DistantStreaming*.ini"):
    ini=path.read_text(encoding="utf-8")
    assert not re.search(r"^\[DrawDistance[^\]]*\]",ini,re.M), (
        f"obsolete visual distance setting in {path.name}"
    )

# Preserve read-only GPU/draw-call telemetry; removal should not mean losing
# frame/streaming observability.
assert (root/"include/asc/RenderCostProbe.h").exists()
assert "DrawProbe" in (root/"include/asc/RenderCostProbe.h").read_text()
print("PASS: no compiled draw-distance controller or visual-distance config")
