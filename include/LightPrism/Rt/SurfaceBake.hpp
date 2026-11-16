#pragma once
// CPU surface-sample cache for static ambient occlusion and directional-light visibility.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "LightPrism/Rt/Bvh.hpp"

namespace LightPrism::Rt {

struct SurfaceBakeSample {
    Vec3 position;
    Vec3 normal;
};

struct SurfaceBakeValue {
    float ambientOcclusion = 1.0f;
    float sunVisibility = 1.0f;
};

struct SurfaceBakeConfig {
    float aoRadius = 0.6f;
    float shadowMaxDistance = 30.0f;
    uint32_t aoRays = 64;
    float bias = 2e-3f;
};

struct SurfaceBakeStats {
    uint32_t samples = 0;
    uint32_t dirty = 0;
    uint64_t raysCast = 0;
    uint64_t bytes = 0;
};

class SurfaceBakeCache {
public:
    // Samples are kept in caller order, so the host can associate each entry with its surface/UV.
    bool Init(std::vector<SurfaceBakeSample> samples, const SurfaceBakeConfig& config, const Vec3& toSun);
    bool SetSunDirection(const Vec3& toSun);
    uint32_t Bake(const Bvh& bvh, uint32_t budget = 0);
    uint32_t InvalidateRegion(const Aabb& changed);
    void InvalidateAll();

    bool Get(uint32_t index, SurfaceBakeValue& value) const;
    uint32_t Count() const { return uint32_t(m_samples.size()); }
    const SurfaceBakeSample& Sample(uint32_t index) const { return m_samples[index]; }
    const SurfaceBakeConfig& Config() const { return m_config; }
    const Vec3& SunDirection() const { return m_toSun; }
    SurfaceBakeStats Stats() const;

    // Versioned little-endian cache data, including sample order and dirty flags.
    bool Serialize(std::vector<uint8_t>& bytes) const;
    bool Deserialize(const uint8_t* bytes, size_t size);

private:
    void BakeOne(const Bvh& bvh, uint32_t index);

    SurfaceBakeConfig m_config;
    Vec3 m_toSun{0, 1, 0};
    std::vector<SurfaceBakeSample> m_samples;
    std::vector<SurfaceBakeValue> m_values;
    std::vector<uint8_t> m_dirty;
    uint32_t m_cursor = 0;
    uint32_t m_dirtyCount = 0;
    uint64_t m_raysCast = 0;
};

} // namespace LightPrism::Rt
