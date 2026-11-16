#pragma once
// Experimental CPU/GPU ray-work scheduler. GPU work is supplied by the host; this module never maps RAM as VRAM.
#include <cstdint>
#include <vector>

#include "LightPrism/Rt/Bvh.hpp"

namespace LightPrism::Rt {

struct MemoryartnerConfig {
    bool enabled = false;
    float cpuShare = 0.25f;
    uint32_t maxCpuRaysPerBatch = 4096;
    uint64_t cpuBvhMemoryBudgetBytes = 512ull * 1024 * 1024;
    uint64_t gpuMemoryBudgetBytes = 0;    // 0 = unknown; caller reports its own conservative budget
    uint64_t gpuMemoryUsedBytes = 0;
    uint32_t gpuBytesPerRay = 64;         // host-provided estimate for request/result staging on GPU
};

struct TraceRequest {
    Ray ray;
    bool anyHit = false;                  // true = occlusion query; false = closest hit
};

struct TraceResult {
    bool hit = false;
    Hit closest;
};

using GpuTraceBatch = bool (*)(const TraceRequest* requests, uint32_t count, TraceResult* results, void* context);

enum class MemoryartnerStatus : uint8_t {
    Success,
    InvalidInput,
    CpuMemoryBudgetExceeded,
    GpuMemoryBudgetExceeded,
    GpuCallbackFailed
};

struct MemoryartnerPlan {
    MemoryartnerStatus status = MemoryartnerStatus::Success;
    uint32_t cpuRays = 0;
    uint32_t gpuRays = 0;
    bool gpuBudgetLimited = false;
};

struct MemoryartnerStats {
    uint32_t requestedRays = 0;
    uint32_t cpuRays = 0;
    uint32_t gpuRays = 0;
    uint64_t cpuBvhBytes = 0;
    bool gpuBudgetLimited = false;
};

MemoryartnerPlan PlanMemoryartner(uint32_t rayCount, const MemoryartnerConfig& config, uint64_t cpuBvhBytes,
                                  bool gpuAvailable);

MemoryartnerStatus ExecuteMemoryartner(const Bvh& bvh, const TraceRequest* requests, uint32_t count,
                                       const MemoryartnerConfig& config, GpuTraceBatch gpuTrace, void* gpuContext,
                                       std::vector<TraceResult>& results, MemoryartnerStats* stats = nullptr);

} // namespace LightPrism::Rt
