#pragma once
#include <string_view>

namespace asc::coexistence {

// Match a loaded module's basename (not an arbitrary substring of its path).
// Module discovery is repeated at runtime because ASI load order is unspecified.
constexpr wchar_t asciiLower(wchar_t c) noexcept {
    return c >= L'A' && c <= L'Z' ? wchar_t(c - L'A' + L'a') : c;
}
constexpr bool equalsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (asciiLower(a[i]) != asciiLower(b[i])) return false;
    return true;
}
constexpr std::wstring_view basename(std::wstring_view name) noexcept {
    const auto pos = name.find_last_of(L"\\/");
    return pos == std::wstring_view::npos ? name : name.substr(pos + 1);
}
constexpr bool isDistantStreaming(std::wstring_view path) noexcept {
    auto n = basename(path);
    return equalsIgnoreCase(n, L"DistantStreaming.asi") ||
           equalsIgnoreCase(n, L"DistantStreaming.SA.asi") ||
           equalsIgnoreCase(n, L"DistantStreaming.dll") ||
           // Legacy module names are accepted for coexistence detection only.
           equalsIgnoreCase(n, L"AdaptiveStreaming.asi") ||
           equalsIgnoreCase(n, L"AdaptiveStreaming.SA.asi") ||
           equalsIgnoreCase(n, L"AdaptiveStreaming.dll");
}
// Retained for legacy SanFrameOptimizer call sites.
constexpr bool isAdaptiveStreaming(std::wstring_view path) noexcept {
    return isDistantStreaming(path);
}
constexpr bool isImprovedStreaming(std::wstring_view path) noexcept {
    auto n = basename(path);
    return equalsIgnoreCase(n, L"ImprovedStreaming.asi") ||
           equalsIgnoreCase(n, L"ImprovedStreaming.SA.asi") ||
           equalsIgnoreCase(n, L"LoadWholeMap.asi") ||
           equalsIgnoreCase(n, L"LoadWholeMap.SA.asi");
}
constexpr bool ownsStreamingScheduler(std::wstring_view path) noexcept {
    return isDistantStreaming(path) || isImprovedStreaming(path);
}
} // namespace asc::coexistence
