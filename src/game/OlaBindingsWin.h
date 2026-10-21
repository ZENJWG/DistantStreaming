#pragma once
#include <ostream>
namespace ola_bindings {
    bool IsOlaLoaded() noexcept;
    // Called after GTA streaming initialization, before ANY model-table
    // iteration. Changes Plugin-SDK static pointers in this ASI only.
    // False means no world traversal or renderer scan is permitted.
    bool Bind(std::ostream& log);
    // Cheap consistency check for OLA's live streaming array, its relocated
    // model-table reference, and our Plugin-SDK snapshots.
    bool StillBound();
}
