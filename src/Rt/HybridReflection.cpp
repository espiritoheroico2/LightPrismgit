#include "LightPrism/Rt/HybridReflection.hpp"

#include <algorithm>
#include <cmath>

namespace LightPrism::Rt {
namespace {

bool Finite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float SmoothStep01(float value) {
    value = Saturate(value);
    return value * value * (3.0f - 2.0f * value);
}

} // namespace

bool TraceHybridReflection(const Bvh& bvh, const Ray& reflectionRay, const HybridReflectionConfig& config,
                           const Vec3& rtRadiance, const ScreenSpaceReflectionSample& ssr,
                           const Vec3& environmentRadiance, HybridReflectionResult& result) {
    if (!std::isfinite(config.maxRtDistance) || !(config.maxRtDistance > 0.0f) ||
        !std::isfinite(config.blendStartDistance) || !Finite(reflectionRay.origin) || !Finite(reflectionRay.dir) ||
        !Finite(rtRadiance) || !Finite(ssr.radiance) || !std::isfinite(ssr.confidence) ||
        !Finite(environmentRadiance))
        return false;

    HybridReflectionResult completed;
    const float ssrConfidence = Saturate(ssr.confidence);
    if (config.enabled) {
        const float directionLength = Length(reflectionRay.dir);
        if (!std::isfinite(directionLength) || !(directionLength > 1e-8f)) return false;

        Ray ray = reflectionRay;
        ray.dir = ray.dir * (1.0f / directionLength);
        ray.tMax = config.maxRtDistance;
        completed.hasRtHit = bvh.Intersect(ray, completed.rtHit);
        if (completed.hasRtHit) {
            completed.rtDistance = completed.rtHit.t;
            const float blendStart = std::min(std::max(config.blendStartDistance, 0.0f), config.maxRtDistance);
            const float blendRange = config.maxRtDistance - blendStart;
            completed.rtWeight = blendRange > 1e-6f
                                     ? 1.0f - SmoothStep01((completed.rtDistance - blendStart) / blendRange)
                                     : (completed.rtDistance < config.maxRtDistance ? 1.0f : 0.0f);
        }
    }

    completed.ssrWeight = (1.0f - completed.rtWeight) * ssrConfidence;
    completed.environmentWeight = 1.0f - completed.rtWeight - completed.ssrWeight;
    completed.radiance = rtRadiance * completed.rtWeight + ssr.radiance * completed.ssrWeight +
                         environmentRadiance * completed.environmentWeight;

    if (completed.rtWeight > 0.0f && completed.ssrWeight > 0.0f)
        completed.source = ReflectionSource::Blended;
    else if (completed.rtWeight > 0.0f)
        completed.source = ReflectionSource::RayTracing;
    else if (completed.ssrWeight > 0.0f)
        completed.source = ReflectionSource::ScreenSpace;
    else
        completed.source = ReflectionSource::Environment;

    result = completed;
    return true;
}

} // namespace LightPrism::Rt
