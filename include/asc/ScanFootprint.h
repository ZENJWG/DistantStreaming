#pragma once
// Conservative, bounded 2D coverage of a 3D camera frustum projected onto
// GTA SA's sector grid. Does not touch GTA memory and is unit-testable.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace asc::scan_footprint {
struct Point { float x{}, y{}; };
struct Polygon {
    std::array<Point, 5> points{};
    unsigned count{};
};
inline float Cross(Point a, Point b, Point c) noexcept {
    return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
}
inline Polygon ConvexHull(std::array<Point, 5> points) noexcept {
    Polygon result{};
    for (const auto& p : points)
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return result;
    std::sort(points.begin(), points.end(), [](Point a, Point b){
        return a.x == b.x ? a.y < b.y : a.x < b.x;
    });
    std::array<Point, 10> hull{};
    unsigned n = 0;
    for (const auto& p : points) {
        if (n && p.x == hull[n-1].x && p.y == hull[n-1].y) continue;
        while (n >= 2 && Cross(hull[n-2], hull[n-1], p) <= 0.f) --n;
        hull[n++] = p;
    }
    const unsigned lower = n;
    for (int i=3; i>=0; --i) {
        const auto& p = points[size_t(i)];
        if (p.x == points[size_t(i+1)].x && p.y == points[size_t(i+1)].y) continue;
        while (n > lower && Cross(hull[n-2], hull[n-1], p) <= 0.f) --n;
        hull[n++] = p;
    }
    if (n > 1) --n; // duplicated first point
    result.count = std::min(n, 5u);
    for (unsigned i=0; i<result.count; ++i) result.points[i] = hull[i];
    return result;
}
// Inclusive polygon-cell overlap. A cell that touches the footprint is scanned.
// For collapsed 1D/point projections, the AABB path conservatively preserves
// nearby sectors rather than silently returning zero scan candidates.
inline bool Overlaps(Polygon const& hull, int x, int y) noexcept {
    if (!hull.count) return false;
    for (unsigned i=0; i<hull.count && hull.count>=3; ++i) {
        const Point a=hull.points[i], b=hull.points[(i+1)%hull.count];
        const float dx=b.x-a.x, dy=b.y-a.y;
        const float bx=dy>=0.f ? float(x) : float(x+1);
        const float by=dx>=0.f ? float(y+1) : float(y);
        const float mostPositive=dx*(by-a.y)-dy*(bx-a.x);
        if (mostPositive < -0.0001f) return false;
    }
    return true;
}
template<class Visit>
uint32_t Rasterize(Polygon const& hull, int width, int height, Visit&& visit) {
    if (!hull.count || width<=0 || height<=0) return 0;
    float minX=hull.points[0].x, maxX=minX;
    float minY=hull.points[0].y, maxY=minY;
    for (unsigned i=1;i<hull.count;++i) {
        const Point& p=hull.points[i];
        minX=std::min(minX,p.x); maxX=std::max(maxX,p.x);
        minY=std::min(minY,p.y); maxY=std::max(maxY,p.y);
    }
    // Clamp to finite sector grid before float->int conversion.
    const int loX=int(std::floor(std::clamp(minX-1.f,0.f,float(width))));
    const int hiX=int(std::ceil(std::clamp(maxX+1.f,0.f,float(width))))-1;
    const int loY=int(std::floor(std::clamp(minY-1.f,0.f,float(height))));
    const int hiY=int(std::ceil(std::clamp(maxY+1.f,0.f,float(height))))-1;
    uint32_t count=0;
    for (int y=loY;y<=hiY;++y) for (int x=loX;x<=hiX;++x) {
        if (!Overlaps(hull,x,y)) continue;
        visit(x,y);
        ++count;
    }
    return count;
}
} // namespace asc::scan_footprint
