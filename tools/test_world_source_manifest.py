"""Portable inventory regression test."""
import importlib.util
from pathlib import Path
import tempfile

module_path = Path(__file__).with_name("world_source_manifest.py")
spec = importlib.util.spec_from_file_location("world_source_manifest", module_path)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    for directory in ("models", "modloader/custom", "data", "unrelated"):
        (root / directory).mkdir(parents=True, exist_ok=True)
    (root / "models" / "gta3.img").write_bytes(b"VER2"*128)
    (root / "modloader" / "custom" / "TREE.DFF").write_bytes(b"geometry")
    (root / "models" / "tree.dff").write_bytes(b"other")
    (root / "data" / "gta.dat").write_text("IMG MODELS\\GTA3.IMG")
    (root / "unrelated" / "unused.txd").write_bytes(b"not scanned")
    report = module.collect(root)
    assert report["candidateFiles"] == 4
    assert report["byKind"]["img"]["bytes"] == 512
    assert report["byKind"]["dff"]["files"] == 2
    assert len(report["duplicateBasenames"]) == 1
    assert report["scanErrors"] == []
