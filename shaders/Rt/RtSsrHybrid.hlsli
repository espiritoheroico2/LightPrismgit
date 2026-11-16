// Hybrid reflection helpers: bounded BVH trace followed by host-generated SSR/environment samples.
#ifndef LP_RT_SSR_HYBRID_HLSLI
#define LP_RT_SSR_HYBRID_HLSLI
#include "Bvh.hlsli"

struct RtSsrHybridHit
{
    bool hit;
    float distance;
    uint triangle;       // leaf-order index; convert through Bvh::LeafOrderIndices() on the host
    float2 barycentrics;
};

RtSsrHybridHit RtSsrHybridTrace(RtRay reflectionRay, float maxRtDistance)
{
    RtSsrHybridHit result;
    result.hit = false;
    result.distance = 0.0f;
    result.triangle = 0;
    result.barycentrics = 0.0f;
    float directionLength = length(reflectionRay.dir);
    if (!isfinite(maxRtDistance) || !(maxRtDistance > 0.0f) ||
        !isfinite(directionLength) || !(directionLength > 1e-8f))
        return result;

    reflectionRay.dir /= directionLength;
    reflectionRay.tMax = maxRtDistance;
    float t = BvhIntersectHit(reflectionRay, result.triangle, result.barycentrics);
    result.hit = t >= 0.0f;
    result.distance = result.hit ? t : 0.0f;
    return result;
}

float3 RtSsrHybridResolve(
    float3 rtRadiance,
    RtSsrHybridHit rtHit,
    float3 ssrRadiance,
    float ssrConfidence,
    float3 environmentRadiance,
    float maxRtDistance,
    float blendStartDistance,
    out float3 sourceWeights)
{
    float rtWeight = 0.0f;
    if (rtHit.hit && maxRtDistance > 0.0f)
    {
        float blendStart = clamp(blendStartDistance, 0.0f, maxRtDistance);
        float blendRange = maxRtDistance - blendStart;
        rtWeight = blendRange > 1e-6f
            ? 1.0f - smoothstep(blendStart, maxRtDistance, rtHit.distance)
            : (rtHit.distance < maxRtDistance ? 1.0f : 0.0f);
    }

    float ssrWeight = (1.0f - rtWeight) * saturate(ssrConfidence);
    float environmentWeight = 1.0f - rtWeight - ssrWeight;
    sourceWeights = float3(rtWeight, ssrWeight, environmentWeight);
    return rtRadiance * rtWeight + ssrRadiance * ssrWeight + environmentRadiance * environmentWeight;
}

#endif
