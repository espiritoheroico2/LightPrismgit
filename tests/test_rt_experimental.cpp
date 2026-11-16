#include <cmath>
#include <cstdio>
#include <vector>

#include "LightPrism/Debug/Visualize.hpp"
#include "LightPrism/Rt/HybridReflection.hpp"
#include "LightPrism/Rt/Memoryartner.hpp"
#include "LightPrism/Rt/SurfaceBake.hpp"

using namespace LightPrism;
using namespace LightPrism::Rt;

static int g_failures = 0;
#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (!(cond)) { std::printf("FALHOU %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failures; } \
    } while (0)

namespace {

Bvh MakeBvh() {
    const Vec3 a{-10, -10, 0.5f}, b{10, -10, 0.5f}, c{10, 10, 0.5f}, d{-10, 10, 0.5f};
    Bvh bvh;
    bvh.Build({Triangle{a, b, c}, Triangle{a, c, d}});
    return bvh;
}

Bvh MakePlane(float z) {
    const Vec3 a{-10, -10, z}, b{10, -10, z}, c{10, 10, z}, d{-10, 10, z};
    Bvh bvh;
    bvh.Build({Triangle{a, b, c}, Triangle{a, c, d}});
    return bvh;
}

bool MockGpuTrace(const TraceRequest* requests, uint32_t count, TraceResult* results, void* context) {
    const Bvh& bvh = *static_cast<const Bvh*>(context);
    for (uint32_t i = 0; i < count; ++i) {
        if (requests[i].anyHit) {
            results[i].hit = bvh.Occluded(requests[i].ray);
        } else {
            results[i].hit = bvh.Intersect(requests[i].ray, results[i].closest);
        }
    }
    return true;
}

bool FailingGpuTrace(const TraceRequest*, uint32_t, TraceResult*, void*) { return false; }

} // namespace

static void TestSurfaceBakeCache() {
    Bvh bvh = MakeBvh();
    SurfaceBakeConfig config;
    config.aoRadius = 0.6f;
    config.aoRays = 128;
    config.shadowMaxDistance = 5.0f;
    SurfaceBakeCache cache;
    CHECK(cache.Init({SurfaceBakeSample{{0, 0, 0}, {0, 0, 1}}}, config, {0, 0, 1}));
    CHECK(cache.Stats().dirty == 1);
    CHECK(cache.Bake(bvh, 1) == 1);

    SurfaceBakeValue value;
    CHECK(cache.Get(0, value));
    CHECK(value.ambientOcclusion < 1.0f);
    CHECK(value.sunVisibility == 0.0f);

    std::vector<uint8_t> bytes;
    CHECK(cache.Serialize(bytes));
    CHECK(bytes.size() == 40 + 33);
    SurfaceBakeCache loaded;
    CHECK(loaded.Deserialize(bytes.data(), bytes.size()));
    SurfaceBakeValue loadedValue;
    CHECK(loaded.Get(0, loadedValue));
    CHECK(std::fabs(loadedValue.ambientOcclusion - value.ambientOcclusion) < 1e-6f);
    CHECK(loadedValue.sunVisibility == value.sunVisibility);

    std::vector<uint8_t> corrupt = bytes;
    corrupt[0] = 0;
    CHECK(!loaded.Deserialize(corrupt.data(), corrupt.size()));
    CHECK(loaded.Get(0, loadedValue));

    Aabb changed;
    changed.Grow(Vec3{-0.1f, -0.1f, -0.1f});
    changed.Grow(Vec3{0.1f, 0.1f, 0.1f});
    CHECK(loaded.InvalidateRegion(changed) == 1);
    CHECK(!loaded.Get(0, loadedValue));
    CHECK(loaded.Bake(bvh) == 1);
    CHECK(loaded.Get(0, loadedValue));
    CHECK(loaded.SetSunDirection({1, 0, 0}));
    CHECK(!loaded.Get(0, loadedValue));
    CHECK(loaded.Bake(bvh) == 1);
    CHECK(loaded.Get(0, loadedValue));
    CHECK(!loaded.SetSunDirection({0, 0, 0}));
    CHECK(loaded.SunDirection().x > 0.99f);
}

static void TestMemoryartnerScheduler() {
    Bvh bvh = MakeBvh();
    std::vector<TraceRequest> requests(10);
    for (uint32_t i = 0; i < requests.size(); ++i) {
        requests[i].ray.origin = (i & 1u) ? Vec3{20, 0, 0} : Vec3{0, 0, 0};
        requests[i].ray.dir = {0, 0, 1};
        requests[i].ray.tMax = 2.0f;
        requests[i].anyHit = (i % 3u) == 0;
    }

    MemoryartnerConfig config;
    config.enabled = true;
    config.cpuShare = 0.3f;
    config.maxCpuRaysPerBatch = 10;
    config.cpuBvhMemoryBudgetBytes = bvh.MemoryBytes() * 2;
    config.gpuMemoryBudgetBytes = 4096;
    config.gpuBytesPerRay = 64;

    MemoryartnerPlan plan = PlanMemoryartner(uint32_t(requests.size()), config, bvh.MemoryBytes(), true);
    CHECK(plan.status == MemoryartnerStatus::Success);
    CHECK(plan.cpuRays == 3 && plan.gpuRays == 7);

    std::vector<TraceResult> results;
    MemoryartnerStats stats;
    CHECK(ExecuteMemoryartner(bvh, requests.data(), uint32_t(requests.size()), config, MockGpuTrace, &bvh,
                              results, &stats) == MemoryartnerStatus::Success);
    CHECK(results.size() == requests.size());
    CHECK(stats.cpuRays == 3 && stats.gpuRays == 7);
    for (uint32_t i = 0; i < requests.size(); ++i) {
        TraceResult expected;
        if (requests[i].anyHit) expected.hit = bvh.Occluded(requests[i].ray);
        else expected.hit = bvh.Intersect(requests[i].ray, expected.closest);
        CHECK(results[i].hit == expected.hit);
        if (expected.hit && !requests[i].anyHit)
            CHECK(std::fabs(results[i].closest.t - expected.closest.t) < 1e-6f);
    }

    config.gpuMemoryBudgetBytes = 128;
    config.gpuMemoryUsedBytes = 64;
    plan = PlanMemoryartner(uint32_t(requests.size()), config, bvh.MemoryBytes(), true);
    CHECK(plan.status == MemoryartnerStatus::Success);
    CHECK(plan.gpuBudgetLimited && plan.cpuRays == 9 && plan.gpuRays == 1);

    config.cpuBvhMemoryBudgetBytes = 1;
    plan = PlanMemoryartner(uint32_t(requests.size()), config, bvh.MemoryBytes(), true);
    CHECK(plan.status == MemoryartnerStatus::GpuMemoryBudgetExceeded);
    CHECK(PlanMemoryartner(1, config, bvh.MemoryBytes(), false).status == MemoryartnerStatus::CpuMemoryBudgetExceeded);

    config.gpuMemoryBudgetBytes = 0;
    config.cpuBvhMemoryBudgetBytes = bvh.MemoryBytes() * 2;
    std::vector<TraceResult> cpuResults;
    CHECK(ExecuteMemoryartner(bvh, requests.data(), uint32_t(requests.size()), config, nullptr, nullptr,
                              cpuResults, &stats) == MemoryartnerStatus::Success);
    CHECK(stats.cpuRays == requests.size() && stats.gpuRays == 0);

    config.enabled = false;
    results.assign(1, TraceResult{true, {}});
    CHECK(ExecuteMemoryartner(bvh, requests.data(), uint32_t(requests.size()), config, FailingGpuTrace, nullptr,
                              results) == MemoryartnerStatus::GpuCallbackFailed);
    CHECK(results.size() == 1 && results[0].hit);
}

static void TestHybridReflections() {
    const Ray ray{{0, 0, 0}, {0, 0, 2}, 1e-4f, 1000.0f};
    const Vec3 rtColor{1, 0, 0}, environment{0, 1, 0};
    const ScreenSpaceReflectionSample ssr{{0, 0, 1}, 1.0f};
    HybridReflectionConfig config;
    config.maxRtDistance = 100.0f;
    config.blendStartDistance = 90.0f;
    HybridReflectionResult result;

    Bvh nearScene = MakePlane(50.0f);
    CHECK(TraceHybridReflection(nearScene, ray, config, rtColor, ssr, environment, result));
    CHECK(result.hasRtHit && std::fabs(result.rtDistance - 50.0f) < 1e-4f);
    CHECK(result.source == ReflectionSource::RayTracing && result.radiance.x == 1.0f);

    Bvh farScene = MakePlane(150.0f);
    CHECK(TraceHybridReflection(farScene, ray, config, rtColor, ssr, environment, result));
    CHECK(!result.hasRtHit && result.source == ReflectionSource::ScreenSpace);
    CHECK(result.radiance.z == 1.0f);

    config.maxRtDistance = 200.0f;
    CHECK(TraceHybridReflection(farScene, ray, config, rtColor, ssr, environment, result));
    CHECK(result.hasRtHit && result.source == ReflectionSource::Blended);
    CHECK(result.rtWeight > 0.0f && result.rtWeight < 1.0f);
    CHECK(std::fabs(result.rtWeight + result.ssrWeight + result.environmentWeight - 1.0f) < 1e-6f);

    config.enabled = false;
    CHECK(TraceHybridReflection(farScene, ray, config, rtColor, ssr, environment, result));
    CHECK(!result.hasRtHit && result.source == ReflectionSource::ScreenSpace);
}

static void TestDebugGeometry() {
    Bvh bvh = MakeBvh();
    std::vector<Debug::Line> lines;
    CHECK(Debug::AppendBvhBounds(bvh, lines) == 12);
    CHECK(lines.size() == 12);
    Debug::BvhOptions limited;
    limited.maxNodes = 0;
    CHECK(Debug::AppendBvhBounds(bvh, lines, limited) == 0);

    LightingSystem lighting;
    PointLightDesc point;
    point.position = {1, 2, 3};
    point.radius = 2.0f;
    CHECK(lighting.AddPointLight(point).IsValid());
    SpotLightDesc spot;
    spot.position = {-1, 2, 3};
    spot.radius = 3.0f;
    spot.direction = {0, -1, 0};
    CHECK(lighting.AddSpotLight(spot).IsValid());
    DirectionalLightDesc sun;
    sun.direction = {0, -1, 0};
    lighting.SetDirectionalLight(sun);
    FrameParams params;
    params.cameraPosition = {0, 2, 4};
    const FrameLightView& frame = lighting.BuildFrame(params);
    Debug::LightingOptions debugOptions;
    debugOptions.segments = 8;
    CHECK(Debug::AppendLighting(frame, lines, debugOptions) > 0);

    const Vec3 heat = Debug::HeatColor(0.5f);
    CHECK(std::fabs(heat.x) < 1e-6f && std::fabs(heat.y - 1.0f) < 1e-6f);
    Debug::AppendRay(Ray{{0, 0, 0}, {0, 0, 1}}, 5.0f, true, lines);
    CHECK(lines.back().end.z == 5.0f);
}

int main() {
    TestSurfaceBakeCache();
    TestMemoryartnerScheduler();
    TestHybridReflections();
    TestDebugGeometry();
    if (g_failures == 0) std::printf("Testes de surface bake e Memoryartner: todos passaram.\n");
    else std::printf("Testes de surface bake e Memoryartner: %d falha(s).\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
