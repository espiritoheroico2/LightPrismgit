#pragma once
// Selects and blends bounded BVH reflections with a host-provided SSR sample.
#include "LightPrism/Rt/Bvh.hpp"

namespace LightPrism::Rt {

struct HybridReflectionConfig {
    bool enabled = true;
    float maxRtDistance = 100.0f;
    float blendStartDistance = 90.0f;
};

struct ScreenSpaceReflectionSample {
    Vec3 radiance{};
    float confidence = 0.0f;
};

enum class ReflectionSource : uint8_t { Environment, ScreenSpace, RayTracing, Blended };

struct HybridReflectionResult {
    Vec3 radiance{};
    Hit rtHit{};
    bool hasRtHit = false;
    float rtDistance = 0.0f;
    float rtWeight = 0.0f;
    float ssrWeight = 0.0f;
    float environmentWeight = 1.0f;
    ReflectionSource source = ReflectionSource::Environment;
};

// `rtRadiance` is shaded by the host from `result.rtHit`; SSR and environment are host samples.
// The reflection ray is normalized internally, so maxRtDistance is measured in world units.
bool TraceHybridReflection(const Bvh& bvh, const Ray& reflectionRay, const HybridReflectionConfig& config,
                           const Vec3& rtRadiance, const ScreenSpaceReflectionSample& ssr,
                           const Vec3& environmentRadiance, HybridReflectionResult& result);

} // namespace LightPrism::Rt
