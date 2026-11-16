#pragma once
// Renderer-independent debug line extraction. The host submits these lines to its own debug renderer.
#include <cstdint>
#include <vector>

#include "LightPrism/LightingSystem.hpp"
#include "LightPrism/Rt/Bvh.hpp"

namespace LightPrism::Debug {

struct Line {
    Vec3 start;
    Vec3 end;
    Vec3 color{1.0f, 1.0f, 1.0f};
};

struct BvhOptions {
    uint32_t maxNodes = 4096;
    uint32_t minDepth = 0;
    uint32_t maxDepth = 0xFFFFFFFFu;
    bool includeInternalNodes = true;
    bool includeLeaves = true;
    Vec3 internalColor{0.1f, 0.65f, 1.0f};
    Vec3 leafColor{0.25f, 1.0f, 0.2f};
};

struct LightingOptions {
    uint32_t maxLights = 256;
    uint32_t segments = 16;
    float markerRadius = 0.15f;
    float directionalLength = 5.0f;
    bool showPointRanges = true;
    bool showSpotCones = true;
    bool showSunDirection = true;
};

// Appends 12 wireframe edges per selected BVH node, bounded by maxNodes.
uint32_t AppendBvhBounds(const Rt::Bvh& bvh, std::vector<Line>& lines, const BvhOptions& options = {});
uint32_t AppendLighting(const FrameLightView& lights, std::vector<Line>& lines, const LightingOptions& options = {});
void AppendRay(const Rt::Ray& ray, float displayDistance, bool hit, std::vector<Line>& lines);

// Blue -> green -> red scalar ramp, useful for AO, SSR confidence, irradiance, and error overlays.
Vec3 HeatColor(float value, float minValue = 0.0f, float maxValue = 1.0f);

} // namespace LightPrism::Debug
