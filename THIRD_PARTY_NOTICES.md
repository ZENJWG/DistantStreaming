# Third-party notices

The dependency versions and archive identities are recorded in `deps.lock`.
Retain the license texts in `licenses/` when distributing derived binaries.

| Component | License text |
| --- | --- |
| MinHook, Tsuda Kageyu and contributors | [MinHook](licenses/MinHook.txt) |
| Dear ImGui, Omar Cornut and contributors | [ImGui](licenses/ImGui.txt) |
| Plugin-SDK layouts, DK22Pac and contributors | [Plugin-SDK](licenses/Plugin-SDK.txt) |
| SafetyHook, cursey and contributors | [Boost](licenses/SafetyHook.txt) |
| Zydis and Zycore | [Zydis](licenses/Zydis.txt), [Zycore](licenses/Zycore.txt) |

Plugin-SDK is used for game structure declarations. The generated build-tree
include adapter forwards its old bundled SafetyHook header to the pinned current
library. Upstream SDK sources remain unchanged; no SDK implementation objects
are linked into SanFrame.

No GTA executable, game assets, private game logs, disassembly dump or local
dependency checkout is included in the source/release packages. Differential
tests optionally read a locally supplied game executable and never launch GTA.
The local BankGothic font is excluded from the source bundle; public builds use
Dear ImGui's scalable default font when the local TTF is absent. Local builds
embed BankGothic by default when available; embedding can be disabled explicitly.
