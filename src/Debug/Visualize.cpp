#include "LightPrism/Debug/Visualize.hpp"

#include <algorithm>
#include <cmath>

namespace LightPrism::Debug {
namespace {

struct NodeAtDepth {
    uint32_t index;
    uint32_t depth;
};

bool Finite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void AddLine(const Vec3& a, const Vec3& b, const Vec3& color, std::vector<Line>& lines) {
    lines.push_back({a, b, color});
}

uint32_t AppendBox(const Vec3& lo, const Vec3& hi, const Vec3& color, std::vector<Line>& lines) {
    Vec3 corners[8];
    for (uint32_t i = 0; i < 8; ++i)
        corners[i] = {i & 1u ? hi.x : lo.x, i & 2u ? hi.y : lo.y, i & 4u ? hi.z : lo.z};

    uint32_t added = 0;
    for (uint32_t i = 0; i < 8; ++i)
        for (uint32_t axis = 0; axis < 3; ++axis)
            if ((i & (1u << axis)) == 0) {
                AddLine(corners[i], corners[i | (1u << axis)], color, lines);
                ++added;
            }
    return added;
}

Vec3 SafeColor(const Vec3& color) {
    return {Saturate(color.x), Saturate(color.y), Saturate(color.z)};
}

void AppendMarker(const Vec3& center, float radius, const Vec3& color, std::vector<Line>& lines) {
    AddLine(center - Vec3{radius, 0, 0}, center + Vec3{radius, 0, 0}, color, lines);
    AddLine(center - Vec3{0, radius, 0}, center + Vec3{0, radius, 0}, color, lines);
    AddLine(center - Vec3{0, 0, radius}, center + Vec3{0, 0, radius}, color, lines);
}

void AppendSphere(const Vec3& center, float radius, uint32_t segments, const Vec3& color, std::vector<Line>& lines) {
    for (uint32_t axis = 0; axis < 3; ++axis) {
        for (uint32_t i = 0; i < segments; ++i) {
            const float a0 = 2.0f * kPi * float(i) / float(segments);
            const float a1 = 2.0f * kPi * float(i + 1) / float(segments);
            Vec3 p0, p1;
            if (axis == 0) {
                p0 = {center.x, center.y + radius * std::cos(a0), center.z + radius * std::sin(a0)};
                p1 = {center.x, center.y + radius * std::cos(a1), center.z + radius * std::sin(a1)};
            } else if (axis == 1) {
                p0 = {center.x + radius * std::cos(a0), center.y, center.z + radius * std::sin(a0)};
                p1 = {center.x + radius * std::cos(a1), center.y, center.z + radius * std::sin(a1)};
            } else {
                p0 = {center.x + radius * std::cos(a0), center.y + radius * std::sin(a0), center.z};
                p1 = {center.x + radius * std::cos(a1), center.y + radius * std::sin(a1), center.z};
            }
            AddLine(p0, p1, color, lines);
        }
    }
}

uint32_t AppendSpotCone(const SpotLightGPU& light, uint32_t segments, const Vec3& color, std::vector<Line>& lines) {
    if (!(light.radius > 0.0f) || !Finite(light.position) || !Finite(light.direction)) return 0;
    const Vec3 direction = Normalize(light.direction);
    const Vec3 up = std::fabs(direction.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 right = Normalize(Cross(up, direction));
    const Vec3 coneUp = Cross(direction, right);
    float cosOuter = light.spotScale > 1e-6f ? -light.spotOffset / light.spotScale : 0.8660254f;
    cosOuter = std::clamp(cosOuter, -0.999f, 0.999f);
    const float angle = std::acos(cosOuter);
    const float baseRadius = std::min(light.radius * std::tan(angle), light.radius * 4.0f);
    const Vec3 baseCenter = light.position + direction * light.radius;
    for (uint32_t i = 0; i < segments; ++i) {
        const float a0 = 2.0f * kPi * float(i) / float(segments);
        const float a1 = 2.0f * kPi * float(i + 1) / float(segments);
        const Vec3 radial0 = right * std::cos(a0) + coneUp * std::sin(a0);
        const Vec3 radial1 = right * std::cos(a1) + coneUp * std::sin(a1);
        const Vec3 p0 = baseCenter + radial0 * baseRadius;
        const Vec3 p1 = baseCenter + radial1 * baseRadius;
        AddLine(p0, p1, color, lines);
        AddLine(light.position, p0, color, lines);
    }
    return 2 * segments;
}

} // namespace

uint32_t AppendBvhBounds(const Rt::Bvh& bvh, std::vector<Line>& lines, const BvhOptions& options) {
    if (bvh.Empty() || options.maxNodes == 0) return 0;
    const std::vector<Rt::BvhNode>& nodes = bvh.Nodes();
    std::vector<NodeAtDepth> stack{{0, 0}};
    uint32_t visited = 0, added = 0;
    while (!stack.empty() && visited < options.maxNodes) {
        const NodeAtDepth current = stack.back();
        stack.pop_back();
        if (current.index >= nodes.size()) continue;
        ++visited;
        const Rt::BvhNode& node = nodes[current.index];
        const bool leaf = node.count > 0;
        if (current.depth >= options.minDepth && current.depth <= options.maxDepth &&
            (leaf ? options.includeLeaves : options.includeInternalNodes)) {
            const Vec3 color = SafeColor(leaf ? options.leafColor : options.internalColor);
            added += AppendBox({node.bmin[0], node.bmin[1], node.bmin[2]},
                               {node.bmax[0], node.bmax[1], node.bmax[2]}, color, lines);
        }
        if (!leaf && current.depth < options.maxDepth) {
            const size_t left = node.leftOrFirst;
            if (left + 1 < nodes.size()) stack.push_back({uint32_t(left + 1), current.depth + 1});
            if (left < nodes.size()) stack.push_back({uint32_t(left), current.depth + 1});
        }
    }
    return added;
}

uint32_t AppendLighting(const FrameLightView& lights, std::vector<Line>& lines, const LightingOptions& options) {
    const uint32_t segments = std::clamp(options.segments, 3u, 64u);
    const float markerRadius = std::isfinite(options.markerRadius) ? std::max(options.markerRadius, 0.0f) : 0.0f;
    uint32_t added = 0, remaining = options.maxLights;

    if (lights.points) {
        const uint32_t count = std::min(lights.pointCount, remaining);
        for (uint32_t i = 0; i < count; ++i) {
            const PointLightGPU& light = lights.points[i];
            const Vec3 color = SafeColor(light.color);
            if (!Finite(light.position)) continue;
            if (markerRadius > 0.0f) { AppendMarker(light.position, markerRadius, color, lines); added += 3; }
            if (options.showPointRanges && std::isfinite(light.radius) && light.radius > 0.0f) {
                added += 3 * segments;
                AppendSphere(light.position, light.radius, segments, color, lines);
            }
        }
        remaining -= count;
    }

    if (lights.spots && remaining > 0) {
        const uint32_t count = std::min(lights.spotCount, remaining);
        for (uint32_t i = 0; i < count; ++i) {
            const SpotLightGPU& light = lights.spots[i];
            const Vec3 color = SafeColor(light.color);
            if (!Finite(light.position)) continue;
            if (markerRadius > 0.0f) { AppendMarker(light.position, markerRadius, color, lines); added += 3; }
            if (options.showSpotCones) added += AppendSpotCone(light, segments, color, lines);
        }
        remaining -= count;
    }

    if (options.showSunDirection && lights.globals.hasSun && remaining > 0 &&
        Finite(lights.globals.cameraPosition) && Finite(lights.sun.direction) &&
        std::isfinite(options.directionalLength) && options.directionalLength > 0.0f) {
        const Vec3 direction = Normalize(lights.sun.direction);
        AddLine(lights.globals.cameraPosition,
                lights.globals.cameraPosition + direction * options.directionalLength,
                SafeColor(lights.sun.color), lines);
        ++added;
    }
    return added;
}

void AppendRay(const Rt::Ray& ray, float displayDistance, bool hit, std::vector<Line>& lines) {
    const float directionLength = Length(ray.dir);
    if (!Finite(ray.origin) || !Finite(ray.dir) || !std::isfinite(directionLength) ||
        !(directionLength > 1e-8f) || !std::isfinite(displayDistance) || !(displayDistance > 0.0f))
        return;
    const Vec3 direction = ray.dir * (1.0f / directionLength);
    AddLine(ray.origin, ray.origin + direction * displayDistance, hit ? Vec3{1, 0.15f, 0.1f} : Vec3{0.1f, 0.9f, 0.3f}, lines);
}

Vec3 HeatColor(float value, float minValue, float maxValue) {
    if (!std::isfinite(value) || !std::isfinite(minValue) || !std::isfinite(maxValue) || !(maxValue > minValue))
        return {1, 0, 1};
    const float t = Saturate((value - minValue) / (maxValue - minValue));
    return t < 0.5f ? Vec3{0, t * 2.0f, 1.0f - t * 2.0f} : Vec3{t * 2.0f - 1.0f, 2.0f - t * 2.0f, 0};
}

} // namespace LightPrism::Debug
