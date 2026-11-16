#include "LightPrism/Rt/SurfaceBake.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "LightPrism/Rt/RtEffects.hpp"

namespace LightPrism::Rt {
namespace {

constexpr uint32_t kCacheVersion = 1;
constexpr size_t kHeaderBytes = 40;
constexpr size_t kRecordBytes = 33;
constexpr uint8_t kMagic[4] = {'L', 'P', 'S', 'B'};

bool Finite(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool ValidConfig(const SurfaceBakeConfig& c) {
    return std::isfinite(c.aoRadius) && c.aoRadius > 0.0f &&
           std::isfinite(c.shadowMaxDistance) && c.shadowMaxDistance > 0.0f &&
           c.aoRays > 0 && std::isfinite(c.bias) && c.bias >= 0.0f;
}

void AppendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(uint8_t(value));
    bytes.push_back(uint8_t(value >> 8));
    bytes.push_back(uint8_t(value >> 16));
    bytes.push_back(uint8_t(value >> 24));
}

void AppendFloat(std::vector<uint8_t>& bytes, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU32(bytes, bits);
}

bool ReadU32(const uint8_t* bytes, size_t size, size_t& cursor, uint32_t& value) {
    if (cursor > size || size - cursor < 4) return false;
    value = uint32_t(bytes[cursor]) | (uint32_t(bytes[cursor + 1]) << 8) |
            (uint32_t(bytes[cursor + 2]) << 16) | (uint32_t(bytes[cursor + 3]) << 24);
    cursor += 4;
    return true;
}

bool ReadFloat(const uint8_t* bytes, size_t size, size_t& cursor, float& value) {
    uint32_t bits = 0;
    if (!ReadU32(bytes, size, cursor, bits)) return false;
    std::memcpy(&value, &bits, sizeof(value));
    return std::isfinite(value);
}

float DistanceSquaredToBox(const Vec3& point, const Aabb& box) {
    const float dx = std::max(std::max(box.lo.x - point.x, 0.0f), point.x - box.hi.x);
    const float dy = std::max(std::max(box.lo.y - point.y, 0.0f), point.y - box.hi.y);
    const float dz = std::max(std::max(box.lo.z - point.z, 0.0f), point.z - box.hi.z);
    return dx * dx + dy * dy + dz * dz;
}

} // namespace

bool SurfaceBakeCache::Init(std::vector<SurfaceBakeSample> samples, const SurfaceBakeConfig& config, const Vec3& toSun) {
    const float sunLength = Length(toSun);
    if (!ValidConfig(config) || !Finite(toSun) || !std::isfinite(sunLength) || !(sunLength > 1e-8f) ||
        samples.size() > std::numeric_limits<uint32_t>::max())
        return false;
    for (SurfaceBakeSample& sample : samples) {
        const float normalLength = Length(sample.normal);
        if (!Finite(sample.position) || !Finite(sample.normal) || !std::isfinite(normalLength) ||
            !(normalLength > 1e-8f))
            return false;
        sample.normal = Normalize(sample.normal);
    }

    m_config = config;
    m_toSun = toSun * (1.0f / sunLength);
    m_samples = std::move(samples);
    m_values.assign(m_samples.size(), SurfaceBakeValue{});
    m_dirty.assign(m_samples.size(), 1);
    m_cursor = 0;
    m_dirtyCount = uint32_t(m_samples.size());
    m_raysCast = 0;
    return true;
}

bool SurfaceBakeCache::SetSunDirection(const Vec3& toSun) {
    const float sunLength = Length(toSun);
    if (!Finite(toSun) || !std::isfinite(sunLength) || !(sunLength > 1e-8f)) return false;
    const Vec3 normalized = toSun * (1.0f / sunLength);
    if (Length(normalized - m_toSun) > 1e-6f) {
        m_toSun = normalized;
        InvalidateAll();
    }
    return true;
}

void SurfaceBakeCache::BakeOne(const Bvh& bvh, uint32_t index) {
    const SurfaceBakeSample& sample = m_samples[index];
    TraversalStats traversal;
    RtAoConfig ao;
    ao.radius = m_config.aoRadius;
    ao.rays = m_config.aoRays;
    ao.bias = m_config.bias;
    m_values[index].ambientOcclusion = RtAmbientOcclusion(bvh, sample.position, sample.normal, ao, index, &traversal);

    RtShadowConfig shadow;
    shadow.maxDistance = m_config.shadowMaxDistance;
    shadow.bias = m_config.bias;
    shadow.samples = 1;
    m_values[index].sunVisibility = RtSunVisibility(bvh, sample.position, sample.normal, m_toSun, shadow, index, &traversal);
    m_raysCast += traversal.rays;
}

uint32_t SurfaceBakeCache::Bake(const Bvh& bvh, uint32_t budget) {
    const uint32_t count = Count();
    const uint32_t limit = budget == 0 ? m_dirtyCount : std::min(budget, m_dirtyCount);
    uint32_t baked = 0;
    for (uint32_t visited = 0; visited < count && baked < limit; ++visited) {
        const uint32_t index = m_cursor;
        m_cursor = (m_cursor + 1) % count;
        if (!m_dirty[index]) continue;
        BakeOne(bvh, index);
        m_dirty[index] = 0;
        --m_dirtyCount;
        ++baked;
    }
    return baked;
}

uint32_t SurfaceBakeCache::InvalidateRegion(const Aabb& changed) {
    if (!changed.Valid()) return 0;
    const float range = std::max(m_config.aoRadius, m_config.shadowMaxDistance);
    const float rangeSquared = range * range;
    uint32_t invalidated = 0;
    for (size_t i = 0; i < m_samples.size(); ++i) {
        if (!m_dirty[i] && DistanceSquaredToBox(m_samples[i].position, changed) <= rangeSquared) {
            m_dirty[i] = 1;
            ++m_dirtyCount;
            ++invalidated;
        }
    }
    return invalidated;
}

void SurfaceBakeCache::InvalidateAll() {
    std::fill(m_dirty.begin(), m_dirty.end(), uint8_t(1));
    m_dirtyCount = Count();
    m_cursor = 0;
}

bool SurfaceBakeCache::Get(uint32_t index, SurfaceBakeValue& value) const {
    if (index >= Count() || m_dirty[index]) return false;
    value = m_values[index];
    return true;
}

SurfaceBakeStats SurfaceBakeCache::Stats() const {
    SurfaceBakeStats stats;
    stats.samples = Count();
    stats.dirty = m_dirtyCount;
    stats.raysCast = m_raysCast;
    stats.bytes = uint64_t(m_samples.size()) * (sizeof(SurfaceBakeSample) + sizeof(SurfaceBakeValue) + sizeof(uint8_t));
    return stats;
}

bool SurfaceBakeCache::Serialize(std::vector<uint8_t>& bytes) const {
    if (Count() > (std::numeric_limits<size_t>::max() - kHeaderBytes) / kRecordBytes) return false;
    std::vector<uint8_t> result;
    result.reserve(kHeaderBytes + size_t(Count()) * kRecordBytes);
    result.insert(result.end(), kMagic, kMagic + sizeof(kMagic));
    AppendU32(result, kCacheVersion);
    AppendU32(result, Count());
    AppendU32(result, m_config.aoRays);
    AppendFloat(result, m_config.aoRadius);
    AppendFloat(result, m_config.shadowMaxDistance);
    AppendFloat(result, m_config.bias);
    AppendFloat(result, m_toSun.x); AppendFloat(result, m_toSun.y); AppendFloat(result, m_toSun.z);
    for (size_t i = 0; i < m_samples.size(); ++i) {
        const SurfaceBakeSample& sample = m_samples[i];
        const SurfaceBakeValue& value = m_values[i];
        AppendFloat(result, sample.position.x); AppendFloat(result, sample.position.y); AppendFloat(result, sample.position.z);
        AppendFloat(result, sample.normal.x); AppendFloat(result, sample.normal.y); AppendFloat(result, sample.normal.z);
        AppendFloat(result, value.ambientOcclusion); AppendFloat(result, value.sunVisibility);
        result.push_back(m_dirty[i]);
    }
    bytes.swap(result);
    return true;
}

bool SurfaceBakeCache::Deserialize(const uint8_t* bytes, size_t size) {
    if (!bytes || size < kHeaderBytes || std::memcmp(bytes, kMagic, sizeof(kMagic)) != 0) return false;
    size_t cursor = sizeof(kMagic);
    uint32_t version = 0, count = 0, rays = 0;
    SurfaceBakeConfig config;
    Vec3 sun;
    if (!ReadU32(bytes, size, cursor, version) || version != kCacheVersion ||
        !ReadU32(bytes, size, cursor, count) || !ReadU32(bytes, size, cursor, rays) ||
        !ReadFloat(bytes, size, cursor, config.aoRadius) ||
        !ReadFloat(bytes, size, cursor, config.shadowMaxDistance) ||
        !ReadFloat(bytes, size, cursor, config.bias) ||
        !ReadFloat(bytes, size, cursor, sun.x) || !ReadFloat(bytes, size, cursor, sun.y) ||
        !ReadFloat(bytes, size, cursor, sun.z) || !ValidConfig(config))
        return false;
    const size_t payloadBytes = size - kHeaderBytes;
    if (payloadBytes % kRecordBytes != 0 || payloadBytes / kRecordBytes != count) return false;
    config.aoRays = rays;
    if (!ValidConfig(config)) return false;

    std::vector<SurfaceBakeSample> samples(count);
    std::vector<SurfaceBakeValue> values(count);
    std::vector<uint8_t> dirty(count);
    for (uint32_t i = 0; i < count; ++i) {
        SurfaceBakeSample& sample = samples[i];
        SurfaceBakeValue& value = values[i];
        if (!ReadFloat(bytes, size, cursor, sample.position.x) || !ReadFloat(bytes, size, cursor, sample.position.y) ||
            !ReadFloat(bytes, size, cursor, sample.position.z) || !ReadFloat(bytes, size, cursor, sample.normal.x) ||
            !ReadFloat(bytes, size, cursor, sample.normal.y) || !ReadFloat(bytes, size, cursor, sample.normal.z) ||
            !ReadFloat(bytes, size, cursor, value.ambientOcclusion) || !ReadFloat(bytes, size, cursor, value.sunVisibility) ||
            value.ambientOcclusion < 0.0f || value.ambientOcclusion > 1.0f ||
            value.sunVisibility < 0.0f || value.sunVisibility > 1.0f)
            return false;
        const uint8_t dirtyFlag = bytes[cursor++];
        if (dirtyFlag > 1) return false;
        dirty[i] = dirtyFlag;
    }

    SurfaceBakeCache loaded;
    if (!loaded.Init(std::move(samples), config, sun)) return false;
    loaded.m_values = std::move(values);
    loaded.m_dirty = std::move(dirty);
    loaded.m_dirtyCount = uint32_t(std::count(loaded.m_dirty.begin(), loaded.m_dirty.end(), uint8_t(1)));
    *this = std::move(loaded);
    return true;
}

} // namespace LightPrism::Rt
