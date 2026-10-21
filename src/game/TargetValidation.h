#pragma once
#include <string>

struct TargetValidation {
    bool ok = false;
    std::string reason;
};

// No configuration or CRC32 required. Fail closed if basic executable
// and hook-address checks fail. This cannot authenticate a game version.
TargetValidation ValidateGameTarget(bool checkArchive = false);
// Nonessential archive profiling can fail safely without disabling
// the core request and load hooks.
bool IsArchiveHookAvailable();
