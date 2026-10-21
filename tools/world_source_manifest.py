#!/usr/bin/env python3
r"""Offline GTA SA source-asset inventory (NOT a RenderWare residency profiler).

Only reads files. It does not infer Mod Loader precedence or modify the game.
Usage: python tools/world_source_manifest.py --game "C:\Games\GTA San Andreas"
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

EXTENSIONS = {".img", ".dff", ".txd", ".col", ".ide", ".ipl", ".dat"}
ROOTS = ("models", "modloader", "data")
MAX_FILES = 150_000


def _candidate_roots(game: Path) -> list[Path]:
    return [game / root for root in ROOTS if (game / root).is_dir()]


def collect(game: Path) -> dict:
    game = game.resolve(strict=True)
    if not game.is_dir():
        raise ValueError("game path is not a directory")
    results = []
    names = defaultdict(list)
    failed = []
    visited = 0
    for root in _candidate_roots(game):
        for path in root.rglob("*"):
            visited += 1
            if visited > MAX_FILES:
                raise RuntimeError(f"inventory limit exceeded ({MAX_FILES} visited paths)")
            try:
                if path.is_symlink() or path.suffix.casefold() not in EXTENSIONS:
                    continue
                if not path.is_file():
                    continue
                st = path.stat()
                relative = path.relative_to(game).as_posix()
                kind = path.suffix.casefold()[1:]
                item = {"path": relative, "kind": kind,
                        "bytes": st.st_size, "mtimeNs": st.st_mtime_ns}
                results.append(item)
                names[path.name.casefold()].append(relative)
            except (OSError, ValueError) as exc:
                failed.append({"path": str(path), "error": str(exc)})
    results.sort(key=lambda value: value["path"].casefold())
    counts = Counter(item["kind"] for item in results)
    bytes_by_kind = Counter()
    for item in results:
        bytes_by_kind[item["kind"]] += item["bytes"]
    return {
        "schema": 1,
        "description": "Candidates only; no effective Mod Loader ordering, runtime world placement, or decoded GPU residency",
        "gameRoot": str(game),
        "generatedUtc": datetime.now(timezone.utc).isoformat(),
        "candidateFiles": len(results),
        "candidateBytes": sum(item["bytes"] for item in results),
        "candidateMiB": round(sum(item["bytes"] for item in results) / (1024**2), 2),
        "byKind": {
            kind: {"files": count, "bytes": bytes_by_kind[kind]}
            for kind, count in sorted(counts.items())
        },
        "duplicateBasenames": {
            name: paths for name, paths in sorted(names.items()) if len(paths) > 1
        },
        "scanErrors": failed,
        "files": results
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, type=Path,
                        help="GTA SA installation root containing models/ and data/")
    parser.add_argument("--output", type=Path, default=Path("world-source-manifest.json"))
    args = parser.parse_args(argv)
    try:
        result = collect(args.game)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
        temporary.replace(args.output)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"Inventory failed: {exc}", file=sys.stderr)
        return 1
    print(f"{result['candidateFiles']} candidate files / {result['candidateMiB']} MiB, "
          f"{len(result['scanErrors'])} scan errors -> {args.output}")
    print("This does NOT prove that all assets are resident or used by GTA.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
