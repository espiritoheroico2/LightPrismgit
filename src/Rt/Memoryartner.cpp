#include "LightPrism/Rt/Memoryartner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

namespace LightPrism::Rt {
namespace {

bool ValidConfig(const MemoryartnerConfig& config) {
    return std::isfinite(config.cpuShare) && config.gpuBytesPerRay > 0;
}

struct ThreadJoiner {
    std::thread& thread;
    ~ThreadJoiner() { if (thread.joinable()) thread.join(); }
};

void TraceCpu(const Bvh& bvh, const TraceRequest& request, TraceResult& result) {
    if (request.anyHit) {
        result.hit = bvh.Occluded(request.ray);
    } else {
        result.hit = bvh.Intersect(request.ray, result.closest);
    }
}

} // namespace

MemoryartnerPlan PlanMemoryartner(uint32_t rayCount, const MemoryartnerConfig& config, uint64_t cpuBvhBytes,
                                  bool gpuAvailable) {
    MemoryartnerPlan plan;
    if (!ValidConfig(config)) {
        plan.status = MemoryartnerStatus::InvalidInput;
        return plan;
    }
    if (!gpuAvailable) {
        if (cpuBvhBytes > config.cpuBvhMemoryBudgetBytes) {
            plan.status = MemoryartnerStatus::CpuMemoryBudgetExceeded;
            return plan;
        }
        plan.cpuRays = rayCount;
        return plan;
    }

    uint32_t cpuRays = 0;
    if (config.enabled && cpuBvhBytes <= config.cpuBvhMemoryBudgetBytes) {
        const double desired = double(rayCount) * double(Saturate(config.cpuShare));
        cpuRays = std::min(config.maxCpuRaysPerBatch, uint32_t(desired));
    }
    uint32_t gpuRays = rayCount - cpuRays;

    if (config.gpuMemoryBudgetBytes != 0) {
        const uint64_t available = config.gpuMemoryUsedBytes < config.gpuMemoryBudgetBytes
                                       ? config.gpuMemoryBudgetBytes - config.gpuMemoryUsedBytes
                                       : 0;
        const uint64_t capacity = available / config.gpuBytesPerRay;
        if (gpuRays > capacity) {
            plan.gpuBudgetLimited = true;
            if (config.enabled && cpuBvhBytes <= config.cpuBvhMemoryBudgetBytes) {
                const uint64_t requiredCpu = uint64_t(rayCount) - capacity;
                if (requiredCpu > config.maxCpuRaysPerBatch) {
                    plan.status = MemoryartnerStatus::GpuMemoryBudgetExceeded;
                    return plan;
                }
                cpuRays = std::max(cpuRays, uint32_t(requiredCpu));
                gpuRays = rayCount - cpuRays;
            } else {
                plan.status = MemoryartnerStatus::GpuMemoryBudgetExceeded;
                return plan;
            }
        }
    }

    if (cpuRays != 0 && cpuBvhBytes > config.cpuBvhMemoryBudgetBytes) {
        plan.status = MemoryartnerStatus::CpuMemoryBudgetExceeded;
        return plan;
    }
    plan.cpuRays = cpuRays;
    plan.gpuRays = gpuRays;
    return plan;
}

MemoryartnerStatus ExecuteMemoryartner(const Bvh& bvh, const TraceRequest* requests, uint32_t count,
                                       const MemoryartnerConfig& config, GpuTraceBatch gpuTrace, void* gpuContext,
                                       std::vector<TraceResult>& results, MemoryartnerStats* stats) {
    if ((!requests && count != 0) || !ValidConfig(config))
        return MemoryartnerStatus::InvalidInput;

    const uint64_t bvhBytes = bvh.MemoryBytes();
    const MemoryartnerPlan plan = PlanMemoryartner(count, config, bvhBytes, gpuTrace != nullptr);
    if (plan.status != MemoryartnerStatus::Success) return plan.status;

    std::vector<TraceResult> completed(count);
    std::vector<TraceRequest> gpuRequests;
    std::vector<uint32_t> gpuIndices;
    std::vector<TraceRequest> cpuRequests;
    std::vector<uint32_t> cpuIndices;
    gpuRequests.reserve(plan.gpuRays);
    gpuIndices.reserve(plan.gpuRays);
    cpuRequests.reserve(plan.cpuRays);
    cpuIndices.reserve(plan.cpuRays);

    for (uint32_t i = 0; i < count; ++i) {
        const bool useCpu = plan.cpuRays != 0 &&
                            (uint64_t(i + 1) * plan.cpuRays / count > uint64_t(i) * plan.cpuRays / count);
        if (useCpu) {
            cpuRequests.push_back(requests[i]);
            cpuIndices.push_back(i);
        } else {
            gpuRequests.push_back(requests[i]);
            gpuIndices.push_back(i);
        }
    }

    if (gpuRequests.size() != plan.gpuRays || cpuRequests.size() != plan.cpuRays)
        return MemoryartnerStatus::InvalidInput;

    std::thread cpuWorker;
    ThreadJoiner joinCpuWorker{cpuWorker};
    if (gpuTrace && !cpuRequests.empty()) {
        cpuWorker = std::thread([&bvh, &cpuRequests, &cpuIndices, &completed]() {
            for (size_t i = 0; i < cpuRequests.size(); ++i)
                TraceCpu(bvh, cpuRequests[i], completed[cpuIndices[i]]);
        });
    } else {
        for (size_t i = 0; i < cpuRequests.size(); ++i)
            TraceCpu(bvh, cpuRequests[i], completed[cpuIndices[i]]);
    }

    bool gpuSucceeded = true;
    if (!gpuRequests.empty()) {
        std::vector<TraceResult> gpuResults(gpuRequests.size());
        gpuSucceeded = gpuTrace(gpuRequests.data(), uint32_t(gpuRequests.size()), gpuResults.data(), gpuContext);
        if (gpuSucceeded)
            for (size_t i = 0; i < gpuResults.size(); ++i) completed[gpuIndices[i]] = gpuResults[i];
    }
    if (cpuWorker.joinable()) cpuWorker.join();
    if (!gpuSucceeded) return MemoryartnerStatus::GpuCallbackFailed;

    results.swap(completed);
    if (stats) {
        stats->requestedRays = count;
        stats->cpuRays = plan.cpuRays;
        stats->gpuRays = plan.gpuRays;
        stats->cpuBvhBytes = bvhBytes;
        stats->gpuBudgetLimited = plan.gpuBudgetLimited;
    }
    return MemoryartnerStatus::Success;
}

} // namespace LightPrism::Rt
