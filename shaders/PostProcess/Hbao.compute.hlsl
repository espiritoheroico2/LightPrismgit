// HBAO: versão de referência horizon-based, com os mesmos passos da implementação CPU.
// HbaoMain gera AO bruto; HbaoBlur aplica um filtro bilateral guiado por profundidade.
cbuffer HbaoCB : register(b4) {       // 64 bytes (HbaoGPU)
    float  Proj00, Proj11, Proj22, Proj23;
    uint2  Size;
    float  Radius, Bias;
    uint   Directions, Steps;
    float  Power, BlurDepthRel;
    float  FadeStart, FadeEnd, MaxScreenRadius, Padding;
};

Texture2D<float>  DepthTex  : register(t5);
Texture2D<float4> NormalTex : register(t6);
Texture2D<float>  RawAoTex  : register(t7);
RWTexture2D<float> OutAo    : register(u0);

static const float PI = 3.14159265359;

float LinearDepth(uint2 p) {
    float z = DepthTex.Load(int3(p, 0));
    if (z <= 0.0) return 0.0;
    return Proj23 / (z + Proj22);
}

float3 ViewPos(uint2 p, float d) {
    float2 ndc = (float2(p) + 0.5) / float2(Size) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    return float3(ndc.x * d / Proj00, ndc.y * d / Proj11, -d);
}

float Rotation(uint2 p) {
    static const uint pattern[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return float(pattern[(p.y & 3) * 4 + (p.x & 3)]) * (2.0 * PI / 16.0);
}

float SmoothStep01(float t) { t = saturate(t); return t * t * (3.0 - 2.0 * t); }

[numthreads(8, 8, 1)]
void HbaoMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Size.x || id.y >= Size.y) return;
    float depth = LinearDepth(id.xy);
    if (depth <= 0.0 || Radius <= 0.0 || MaxScreenRadius <= 0.0) { OutAo[id.xy] = 1.0; return; }

    float3 position = ViewPos(id.xy, depth);
    float3 normal = normalize(NormalTex.Load(int3(id.xy, 0)).xyz);
    float2 pixelRadius = min(Radius * float2(Proj00 * Size.x, Proj11 * Size.y) / (2.0 * depth),
                             MaxScreenRadius);
    float biasRatio = saturate(Bias / Radius);
    uint directionCount = clamp(Directions, 1u, 16u);
    uint stepCount = clamp(Steps, 1u, 16u);
    float occlusion = 0.0;
    float rotation = Rotation(id.xy);

    for (uint direction = 0; direction < directionCount; ++direction) {
        float angle = rotation + 2.0 * PI * float(direction) / float(directionCount);
        float2 ray = float2(cos(angle), sin(angle));
        float horizon = 0.0;
        float horizonDistance = Radius;

        for (uint step = 1; step <= stepCount; ++step) {
            float t = float(step) / float(stepCount);
            int2 samplePixel = int2(id.xy) + int2(round(ray * pixelRadius * t));
            if (any(samplePixel < 0) || any(samplePixel >= int2(Size)) ||
                all(samplePixel == int2(id.xy)))
                continue;

            float sampleDepth = LinearDepth(uint2(samplePixel));
            if (sampleDepth <= 0.0) continue;
            float3 delta = ViewPos(uint2(samplePixel), sampleDepth) - position;
            float distance = length(delta);
            if (distance <= 1e-5 || distance >= Radius) continue;

            float elevation = dot(normal, delta) / distance;
            float candidate = saturate((elevation - biasRatio) / max(1.0 - biasRatio, 1e-4));
            if (candidate > horizon) {
                horizon = candidate;
                horizonDistance = distance;
            }
        }

        occlusion += horizon * saturate(1.0 - horizonDistance / Radius);
    }

    float ao = saturate(1.0 - occlusion / float(directionCount));
    float shaped = Power == 1.0 ? ao : pow(ao, max(Power, 0.0));
    float fade = SmoothStep01((depth - FadeStart) / max(FadeEnd - FadeStart, 1e-3));
    OutAo[id.xy] = lerp(shaped, 1.0, fade);
}

[numthreads(8, 8, 1)]
void HbaoBlur(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Size.x || id.y >= Size.y) return;
    float depth = LinearDepth(id.xy);
    if (depth <= 0.0) { OutAo[id.xy] = 1.0; return; }

    float sum = 0.0, weightSum = 0.0;
    for (int dy = -2; dy <= 1; ++dy)
        for (int dx = -2; dx <= 1; ++dx) {
            int2 samplePixel = int2(id.xy) + int2(dx, dy);
            if (any(samplePixel < 0) || any(samplePixel >= int2(Size))) continue;
            float sampleDepth = LinearDepth(uint2(samplePixel));
            if (sampleDepth <= 0.0 || abs(sampleDepth - depth) > BlurDepthRel * depth) continue;
            sum += RawAoTex.Load(int3(samplePixel, 0));
            weightSum += 1.0;
        }
    OutAo[id.xy] = weightSum > 0.0 ? sum / weightSum : RawAoTex.Load(int3(id.xy, 0));
}
