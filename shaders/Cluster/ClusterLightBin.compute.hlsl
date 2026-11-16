// Compacta, em ordem de prioridade, as luzes que cobrem cada fatia de profundidade.
// Cada grupo processa uma fatia; reduz o trabalho dos passes por cluster de todas as luzes
// para apenas os candidatos da fatia. Luzes que cobrem todo o frustum continuam candidatas em todas.
#include "../Common/Cluster.hlsli"

cbuffer ClusterGridCB : register(b0) { ClusterGrid Grid; };

StructuredBuffer<ClusterLight> Lights : register(t0);
RWStructuredBuffer<uint> SliceLightCounts : register(u0);
RWStructuredBuffer<uint> SliceLightIndices : register(u1);

[numthreads(1, 1, 1)]
void CSMain(uint3 groupId : SV_GroupID) {
    uint slice = groupId.x;
    if (slice >= Grid.gridZ) return;

    uint total = Grid.numPointLights + Grid.numSpotLights;
    uint stride = max(total, 1u);
    uint count = 0;
    for (uint li = 0; li < total; ++li) {
        ClusterLight light = Lights[li];
        if (int(slice) < light.sliceMin || int(slice) > light.sliceMax) continue;
        SliceLightIndices[slice * stride + count] = li;
        ++count;
    }
    SliceLightCounts[slice] = count;
}
