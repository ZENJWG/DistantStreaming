# Removed executable checksum requirement

- Deleted `[Compatibility] ExpectedExeCRC32` from the default INI.
- Deleted the executable-checksum helper script (`tools/fingerprint_exe.py`).
- Removed CRC32 reading and configuration from `ConfigWin`.
- Replaced the runtime's CRC-gated verifier with `TargetValidation`.
- Kept automatic PE (x86, expected image base) and hook entry-point address checks.
- Updated CMake, README, bootstrap success message, and build validation notes.

## Operational behavior

No user setup or executable checksum is required to enable the controller.
If basic executable checks fail, the plugin does not install detours.
These lightweight checks **cannot distinguish every GTA SA release**;
only use an unmodified PC 1.0 US executable.

The later [LAA-aware memory feature](STREAMING_MEMORY.md) optionally manages
the streaming budget. Vanilla eviction remains in control. This project does
not incorporate LoadWholeMap.
