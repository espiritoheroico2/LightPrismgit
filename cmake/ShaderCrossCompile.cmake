find_program(LP_DXC_EXECUTABLE NAMES dxc)
find_program(LP_SPIRV_CROSS_EXECUTABLE NAMES spirv-cross)
if(NOT LP_DXC_EXECUTABLE OR NOT LP_SPIRV_CROSS_EXECUTABLE)
    message(FATAL_ERROR
        "LP_BUILD_SHADER_TRANSLATIONS requer dxc e spirv-cross. Instale ambos ou desligue a opcao.")
endif()

file(GLOB_RECURSE LP_SHADER_TRANSLATION_INCLUDES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/shaders/*.hlsli"
    "${CMAKE_SOURCE_DIR}/shaders/*.hlsl")

set(LP_SHADER_TRANSLATION_OUTPUTS)
set(LP_SHADER_TRANSLATION_DIR "${CMAKE_CURRENT_BINARY_DIR}/shader-translations")

function(lp_add_shader_translation NAME FILE ENTRY)
    set(SOURCE "${CMAKE_SOURCE_DIR}/shaders/${FILE}")
    set(SPIRV "${LP_SHADER_TRANSLATION_DIR}/${NAME}.spv")
    set(GLSL "${LP_SHADER_TRANSLATION_DIR}/${NAME}.glsl")

    add_custom_command(
        OUTPUT "${SPIRV}" "${GLSL}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LP_SHADER_TRANSLATION_DIR}"
        COMMAND "${LP_DXC_EXECUTABLE}" -spirv -T cs_6_0 -E "${ENTRY}"
                -fspv-target-env=vulkan1.1 -fvk-use-dx-layout
                -I "${CMAKE_SOURCE_DIR}/shaders" -Fo "${SPIRV}" "${SOURCE}"
        COMMAND "${LP_SPIRV_CROSS_EXECUTABLE}" "${SPIRV}" --version 450 --output "${GLSL}"
        DEPENDS "${SOURCE}" ${LP_SHADER_TRANSLATION_INCLUDES}
        COMMENT "Compiling ${FILE}:${ENTRY} to SPIR-V and GLSL"
        VERBATIM)

    set(LP_SHADER_TRANSLATION_OUTPUTS
        ${LP_SHADER_TRANSLATION_OUTPUTS} "${SPIRV}" "${GLSL}" PARENT_SCOPE)
endfunction()

lp_add_shader_translation(cluster-light-prep "Cluster/ClusterLightPrep.compute.hlsl" CSMain)
lp_add_shader_translation(cluster-light-bin "Cluster/ClusterLightBin.compute.hlsl" CSMain)
lp_add_shader_translation(cluster-cull-count "Cluster/ClusterCullCount.compute.hlsl" CSMain)
lp_add_shader_translation(cluster-scan-local "Cluster/ClusterScan.compute.hlsl" ScanLocal)
lp_add_shader_translation(cluster-scan-sums "Cluster/ClusterScan.compute.hlsl" ScanGroupSums)
lp_add_shader_translation(cluster-cull-fill "Cluster/ClusterCullFill.compute.hlsl" CSMain)
lp_add_shader_translation(ssao "PostProcess/Ssao.compute.hlsl" SsaoMain)
lp_add_shader_translation(ssao-blur "PostProcess/Ssao.compute.hlsl" SsaoBlur)
lp_add_shader_translation(hbao "PostProcess/Hbao.compute.hlsl" HbaoMain)
lp_add_shader_translation(hbao-blur "PostProcess/Hbao.compute.hlsl" HbaoBlur)
lp_add_shader_translation(rt-ao "Rt/RtAo.compute.hlsl" main)
lp_add_shader_translation(fog-scatter "Volumetric/FogScatter.compute.hlsl" CSMain)

add_custom_target(LightPrismTranslateShaders DEPENDS ${LP_SHADER_TRANSLATION_OUTPUTS})
