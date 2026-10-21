# Review fixes and validation - 2026-10-16

All edits and rebuilt artifacts are confined to this project.

## Bugs fixed

- The Win32 ASI failed compilation because `Runtime::lastBindingCheck` was
  referenced without a declaration. Declare its timestamp and initialize it
  when binding succeeds so the periodic check waits its intended interval.
- The HD service disconnected immediately after writing. Windows discards
  unread output on `DisconnectNamedPipe`, so clients could receive a truncated
  reply. Flush successful replies before disconnecting. The smoke test exposed
  this failure before the fix.
- Hook-integrity tests used assertions but were missing from the CMake list
  that undefines `NDEBUG`. Keep those assertions active in Release builds.
- Nonfinite candidate scores reached `std::sort` and could violate its ordering
  contract. Discard them before ranking. Reject infinite teleport thresholds.
- Streaming pressure percentage multiplication could overflow with extreme
  uint64 counters. Compare against overflow-safe integer thresholds, preserving
  the existing integer-percent hysteresis boundaries.
- Several runtime diagnostics emitted literal backslash-n characters; the
  activity log even used a multicharacter character literal. Emit newlines.

## Redundancies removed

- Resume static admission from a saved cursor after returning unused dynamic
  quota. Previously rejected static models were checked and counted as denied
  twice in the same tick. Existing total, backlog, and outstanding caps remain.
- Remove repeated DDS name validation from the catalog loop: `InspectDDS`
  already checks the complete stem before converting it to ASCII.
- Guard the service's Windows macro definition and explicitly type its wide
  character conversion, eliminating two service build warnings.

## Validation

- Built Win32 Release ASI and x64 Release helper with the existing local
  dependencies. No dependency download or update was needed.
- All 16 CTest suites passed on Win32 and x64.
- Added scheduler regressions for invalid scores, infinite configuration,
  single-pass denial accounting, returned dynamic quota, extreme memory
  counters, and unchanged recovery boundaries.
- Expanded and passed the Windows HD service smoke test: ping, catalog offsets,
  small and 128 KiB texture payloads, changed DDS metadata, and missing assets.
  Connection establishment now tolerates the interval in which the service
  recreates its single pipe instance; reply failures are not retried.
- `python -S tools/audit_separation.py` passed.
- Refreshed `dist` ASI, matching PDB/map, and x64 helper from these builds.

## Remaining limits

No game launch, live OLA/hook test, D3D reset test, VRAM residency test, or FPS
benchmark was performed. These changes do not establish gameplay stability or
a measured speed improvement. The service transport remains synchronous and
single-client; a client that stops consuming data can hold up the service.
Third-party Plugin-SDK warnings remain in the Win32 build. Earlier review notes
contain additional runtime validation work.
