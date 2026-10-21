#include "asc/StreamingCoexistence.h"
#include <cassert>
using namespace asc::coexistence;
int main() {
  static_assert(isDistantStreaming(L"DistantStreaming.asi"));
  static_assert(isDistantStreaming(L"C:\\Games\\GTA\\DISTANTSTREAMING.ASI"));
  static_assert(isDistantStreaming(L"mods/DistantStreaming.SA.asi"));
  static_assert(isDistantStreaming(L"DistantStreaming.dll"));
  static_assert(isAdaptiveStreaming(L"AdaptiveStreaming.asi"));
  static_assert(isAdaptiveStreaming(L"C:\\Games\\GTA\\ADAPTIVESTREAMING.ASI"));
  static_assert(isAdaptiveStreaming(L"mods/AdaptiveStreaming.SA.asi"));
  static_assert(isAdaptiveStreaming(L"AdaptiveStreaming.dll"));
  static_assert(isImprovedStreaming(L"ImprovedStreaming.SA.asi"));
  static_assert(isImprovedStreaming(L"LoadWholeMap.asi"));
  static_assert(!isDistantStreaming(L"NotDistantStreaming.asi"));
  static_assert(!isDistantStreaming(L"DistantStreaming.asi.bak"));
  static_assert(!isAdaptiveStreaming(L"NotAdaptiveStreaming.asi"));
  static_assert(!isAdaptiveStreaming(L"AdaptiveStreaming.asi.bak"));
  static_assert(!ownsStreamingScheduler(L"SanFrameOptimizer.asi"));
  assert(ownsStreamingScheduler(L"C:\\mods\\AdaptiveStreaming.asi"));
  assert(ownsStreamingScheduler(L"C:\\mods\\DistantStreaming.asi"));
}
