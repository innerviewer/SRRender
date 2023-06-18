//
// Created by Monika on 06.06.2023.
//

#include <Graphics/Pass/CascadedShadowMapPass.h>

namespace SR_GRAPH_NS {
    SR_REGISTER_RENDER_PASS(CascadedShadowMapPass);

    CascadedShadowMapPass::CascadedShadowMapPass(RenderTechnique* pTechnique, BasePass* pParent)
        : Super(pTechnique, pParent)
    { }

    bool CascadedShadowMapPass::Init() {
        return Super::Init();
    }

    void CascadedShadowMapPass::DeInit() {
        Super::DeInit();
    }

    bool CascadedShadowMapPass::Load(const SR_XML_NS::Node& passNode) {
        m_cascadesCount = passNode.TryGetAttribute("Cascades").ToUInt64(4);
        return Super::Load(passNode);
    }

    MeshClusterTypeFlag CascadedShadowMapPass::GetClusterType() const noexcept {
        return static_cast<uint64_t>(MeshClusterType::Opaque) | static_cast<uint64_t>(MeshClusterType::Transparent);
    }

    void CascadedShadowMapPass::UseSharedUniforms(IMeshClusterPass::ShaderPtr pShader) {
        if (m_camera) {
            pShader->SetMat4(SHADER_VIEW_MATRIX, m_camera->GetViewTranslateRef());
            pShader->SetMat4(SHADER_PROJECTION_MATRIX, m_camera->GetProjectionRef());
        }
    }

    void CascadedShadowMapPass::UseUniforms(IMeshClusterPass::ShaderPtr pShader, IMeshClusterPass::MeshPtr pMesh) {
        pMesh->UseModelMatrix();

        UpdateCascades();

        pShader->SetMat4(SHADER_LIGHT_SPACE_MATRIX, m_cascades[1].viewProjMatrix);

        SR_MATH_NS::FVector3 lightPos = GetRenderScene()->GetLightSystem()->m_position;
        pShader->SetVec3(SHADER_DIRECTIONAL_LIGHT_POSITION, lightPos);
    }

    void CascadedShadowMapPass::UpdateCascades() {
        if (!m_camera) {
            return;
        }

        SR_MATH_NS::FVector3 lightPos = GetRenderScene()->GetLightSystem()->m_position;

        std::vector<float_t> cascadeSplits;
        cascadeSplits.resize(m_cascadesCount);

        m_cascades.resize(m_cascadesCount);

        const float_t nearClip = m_camera->GetNear();
        const float_t farClip = m_camera->GetFar();
        const float_t clipRange = farClip - nearClip;

        const float_t minZ = nearClip;
        const float_t maxZ = nearClip + clipRange;

        const float_t range = maxZ - minZ;
        const float_t ratio = maxZ / minZ;

        for (uint32_t i = 0; i < m_cascadesCount; i++) {
            const float_t p = (i + 1) / static_cast<float_t>(m_cascadesCount);
            const float_t log = minZ * std::pow(ratio, p);
            const float_t uniform = minZ + range * p;
            const float_t d = m_cascadeSplitLambda * (log - uniform) + uniform;
            cascadeSplits[i] = (d - nearClip) / clipRange;
        }

        float_t lastSplitDist = 0.0;

        for (uint32_t i = 0; i < m_cascadesCount; i++) {
            const float_t splitDist = cascadeSplits[i];

            SR_MATH_NS::FVector3 frustumCorners[8] = {
                SR_MATH_NS::FVector3(-1.0f,  1.0f, -1.0f),
                SR_MATH_NS::FVector3( 1.0f,  1.0f, -1.0f),
                SR_MATH_NS::FVector3( 1.0f, -1.0f, -1.0f),
                SR_MATH_NS::FVector3(-1.0f, -1.0f, -1.0f),
                SR_MATH_NS::FVector3(-1.0f,  1.0f,  1.0f),
                SR_MATH_NS::FVector3( 1.0f,  1.0f,  1.0f),
                SR_MATH_NS::FVector3( 1.0f, -1.0f,  1.0f),
                SR_MATH_NS::FVector3(-1.0f, -1.0f,  1.0f),
            };

            auto&& invCamera = (m_camera->GetProjectionRef() * m_camera->GetViewTranslateRef()).Inverse();

            for (uint32_t j = 0; j < 8; j++) {
                SR_MATH_NS::FVector4 invCorner = invCamera * SR_MATH_NS::FVector4(frustumCorners[j], 1.0f);
                frustumCorners[j] = (invCorner / invCorner.w).XYZ();
            }

            for (uint32_t j = 0; j < 4; j++) {
                SR_MATH_NS::FVector3 dist = frustumCorners[j + 4] - frustumCorners[j];
                frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
                frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
            }

            SR_MATH_NS::FVector3 frustumCenter = SR_MATH_NS::FVector3(0.0f);
            for (uint32_t j = 0; j < 8; j++) {
                frustumCenter += frustumCorners[j];
            }
            frustumCenter /= 8.0f;

            float_t radius = 0.0f;
            for (uint32_t j = 0; j < 8; j++) {
                float_t distance = (frustumCorners[j] - frustumCenter).Length();
                radius = SR_MAX(radius, distance);
            }
            radius = std::ceil(radius * 16.0f) / 16.0f;

            SR_MATH_NS::FVector3 maxExtents = SR_MATH_NS::FVector3(radius);
            SR_MATH_NS::FVector3 minExtents = -maxExtents;

            SR_MATH_NS::FVector3 lightDir = (-lightPos).Normalize();

            SR_MATH_NS::Matrix4x4 lightViewMatrix = SR_MATH_NS::Matrix4x4::LookAt(frustumCenter - lightDir * -minExtents.z, frustumCenter, SR_MATH_NS::FVector3(0.0f, 1.0f, 0.0f));
            SR_MATH_NS::Matrix4x4 lightOrthoMatrix = SR_MATH_NS::Matrix4x4::Ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

            m_cascades[i].splitDepth = (m_camera->GetNear() + splitDist * clipRange) * -1.0f;
            m_cascades[i].viewProjMatrix = lightOrthoMatrix * lightViewMatrix;
            //m_cascades[i].viewProjMatrix = lightOrthoMatrix * m_camera->GetViewTranslateRef();

            lastSplitDist = cascadeSplits[i];
        }
    }

    const ShadowMapCascade& CascadedShadowMapPass::GetCascade(uint32_t index) const {
        if (m_cascades.size() <= index) {
            SRHalt("Out of range!");
            static ShadowMapCascade cascade;
            return cascade;
        }

        return m_cascades.at(index);
    }
}