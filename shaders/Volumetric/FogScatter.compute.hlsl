// Neblina volumétrica: espalhamento + integração ao longo da coluna (espelha include/LightPrism/Volumetric/Fog.hpp).
// NÃO COMPILADO — validar na engine. Uma thread por coluna (x,y) do volume de froxels; escreve (L.rgb, T) acumulados até a borda FAR
// de cada slice em um RWTexture3D<float4> (RGBA16F). O pixel lê com SampleLevel trilinear na profundidade do pixel:
//     cor = cor * T + L.
// Visibilidade do sol: Common/Shadow.hlsli (CsmVisibility). Troque por VSM/SDF/RT conforme o preset.
#include "../Common/Shadow.hlsli"

cbuffer FogData : register(b7)
{
    row_major float4x4 fogInvView;    // view -> mundo
    float3 fogSunToLight; float fogSigmaT;
    float3 fogSunRadiance; float fogHeightFalloff;
    float3 fogScatterAlbedo; float fogAnisotropy;
    float3 fogAmbient; float fogNear;
    float fogMaxDistance; float fogProj00; float fogProj11; uint fogGridZ;
    uint fogGridX; uint fogGridY; uint fogPointCount; uint fogPad;
};
struct FogPointLight { float3 position; float radius; float3 radiance; float pad; };
StructuredBuffer<FogPointLight> g_FogPoints : register(t11);
RWTexture3D<float4> g_FogOut : register(u7);

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0f - g2) / (12.5663706144f * pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f));
}

float FogSliceEdge(float k) { return fogNear * pow(fogMaxDistance / fogNear, k / float(fogGridZ)); }
float FogDistanceAttenuation(float d, float radius)
{
    float r = d / radius;
    float r2 = r * r;
    float w = saturate(1.0f - r2 * r2);
    return (w * w) / max(d * d, 1e-4f);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= fogGridX || id.y >= fogGridY) return;
    float ndcX = (id.x + 0.5f) / fogGridX * 2.0f - 1.0f;
    float ndcY = 1.0f - (id.y + 0.5f) / fogGridY * 2.0f;
    float3 dv = float3(ndcX / fogProj00, ndcY / fogProj11, -1.0f);
    float rayLen = length(dv);
    float3 worldDir = normalize(mul(fogInvView, float4(dv, 0.0f)).xyz);
    float3 L = 0; float T = 1.0f;
    for (uint z = 0; z < fogGridZ; ++z)
    {
        float d0 = FogSliceEdge(z), d1 = FogSliceEdge(z + 1), dm = sqrt(d0 * d1);
        float3 p = mul(fogInvView, float4(dv * dm, 1.0f)).xyz;
        float sigmaT = fogSigmaT * (fogHeightFalloff > 0.0f ? exp(-fogHeightFalloff * max(p.y, 0.0f)) : 1.0f);
        float dt = (d1 - d0) * rayLen;
        float vis = CsmVisibility(p, float3(0.0f, 0.0f, 0.0f), dm);
        float3 radiance = fogAmbient + fogSunRadiance * (HenyeyGreenstein(dot(fogSunToLight, worldDir), fogAnisotropy) * vis);
        for (uint i = 0; i < fogPointCount; ++i)
        {
            FogPointLight light = g_FogPoints[i];
            float3 toLight = light.position - p;
            float dist = length(toLight);
            if (dist >= light.radius) continue;
            float cosTheta = dot(toLight / max(dist, 1e-4f), worldDir);
            radiance += light.radiance * (FogDistanceAttenuation(dist, light.radius) * HenyeyGreenstein(cosTheta, fogAnisotropy));
        }
        float3 S = fogScatterAlbedo * sigmaT * radiance;
        float e = exp(-sigmaT * dt);
        if (sigmaT > 1e-9f) L += S * (T * (1.0f - e) / sigmaT);
        T *= e;
        g_FogOut[uint3(id.xy, z)] = float4(L, T);
    }
}
