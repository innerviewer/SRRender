//
// Created by Monika on 02.06.2024.
//

#include <Graphics/Pass/MeshDrawerPass.h>
#include <Graphics/Render/RenderQueue.h>
#include <Graphics/Render/RenderContext.h>
#include <Graphics/Render/RenderScene.h>

#include <Utils/ECS/LayerManager.h>

namespace SR_GRAPH_NS {
    RenderQueue::RenderQueue(RenderStrategy* pStrategy, MeshDrawerPass* pDrawer)
        : Super(this, SR_UTILS_NS::SharedPtrPolicy::Automatic)
        , m_uboManager(Memory::UBOManager::Instance())
        , m_meshDrawerPass(pDrawer)
        , m_renderStrategy(pStrategy)
    {
        m_renderContext = pStrategy->GetRenderContext();
        m_renderScene = pStrategy->GetRenderScene();
        m_pipeline = m_renderContext->GetPipeline().Get();
    }

    void RenderQueue::Register(const MeshRegistrationInfo& info) {
        SR_TRACY_ZONE;

        if (!IsSuitable(info)) SR_UNLIKELY_ATTRIBUTE {
            return;
        }

        PrepareLayers();

        MeshInfo meshInfo;
        meshInfo.pMesh = info.pMesh;
        meshInfo.shaderUseInfo = GetShaderUseInfo(info);
        meshInfo.vbo = info.pMesh->GetVBO();
        meshInfo.priority = info.priority.value_or(0);

        for (auto&& [layer, queue] : m_queues) {
            if (layer == info.layer) {
                queue.Add(meshInfo);
                break;
            }
        }
    }

    void RenderQueue::UnRegister(const MeshRegistrationInfo& info) {
        SR_TRACY_ZONE;

        RenderQueue::Queue* pQueue = nullptr;

        for (auto&& [layer, queue] : m_queues) {
            if (layer == info.layer) {
                pQueue = &queue;
                break;
            }
        }

        if (!pQueue) SR_UNLIKELY_ATTRIBUTE {
            return;
        }

        if (info.priority.has_value() && !m_meshDrawerPass->IsPriorityAllowed(info.priority.value())) {
            return;
        }

        MeshInfo meshInfo;
        meshInfo.pMesh = info.pMesh;
        meshInfo.shaderUseInfo = GetShaderUseInfo(info);
        meshInfo.vbo = info.pMesh->GetVBO();
        meshInfo.priority = info.priority.value_or(0);

        if (!pQueue->Remove(meshInfo)) {
            SRHalt("RenderQueue::UnRegister() : mesh not found!");
        }
    }

    bool RenderQueue::Render() {
        SR_TRACY_ZONE;

        PrepareLayers();

        m_rendered = false;

        for (auto&& [layer, queue] : m_queues) {
            Render(layer, queue);
        }

        return m_rendered;
    }

    void RenderQueue::Update() {
        SR_TRACY_ZONE;

        if (!m_rendered) {
            return;
        }

        for (auto&& [layer, queue] : m_queues) {
            Update(layer, queue);
        }
    }

    bool RenderQueue::IsSuitable(const MeshRegistrationInfo &info) const {
        SR_TRACY_ZONE;

        if (!m_meshDrawerPass->IsLayerAllowed(info.layer)) SR_UNLIKELY_ATTRIBUTE {
            return false;
        }

        if (info.priority.has_value() && !m_meshDrawerPass->IsPriorityAllowed(info.priority.value())) SR_UNLIKELY_ATTRIBUTE {
            return false;
        }

        return true;
    }

    void RenderQueue::Render(const SR_UTILS_NS::StringAtom& layer, RenderQueue::Queue& queue) {
        SR_TRACY_ZONE_S(layer.c_str());

        ShaderPtr pCurrentShader = nullptr;
        VBO currentVBO = 0;

        MeshInfo* pStart = queue.data();
        const MeshInfo* pEnd = pStart + queue.size();
        bool shaderOk = false;

        for (MeshInfo* pElement = pStart; pElement < pEnd; ++pElement) {
            const MeshInfo info = *pElement;

            if (!info.shaderUseInfo.pShader || pElement->vbo == SR_ID_INVALID) SR_UNLIKELY_ATTRIBUTE {
                pElement->state = QUEUE_STATE_ERROR;
                continue;
            }

            if (!info.pMesh->IsMeshActive()) SR_UNLIKELY_ATTRIBUTE {
                pElement->state = QUEUE_STATE_ERROR;
                continue;
            }

            if (info.shaderUseInfo.pShader != pCurrentShader) SR_UNLIKELY_ATTRIBUTE {
                pCurrentShader = info.shaderUseInfo.pShader;
                shaderOk = UseShader(info.shaderUseInfo);
                if (!shaderOk) SR_UNLIKELY_ATTRIBUTE {
                    pElement->state = QUEUE_STATE_SHADER_ERROR;
                    pElement = FindNextShader(queue, pElement);
                    continue;
                }
            }

            if (info.vbo != currentVBO) SR_UNLIKELY_ATTRIBUTE {
                if (!info.pMesh->BindMesh()) SR_UNLIKELY_ATTRIBUTE {
                    pElement->state = QUEUE_STATE_VBO_ERROR;
                    pElement = FindNextVBO(queue, pElement);
                    continue;
                }
                currentVBO = info.vbo;
            }

            info.pMesh->Draw();
            pElement->state = QUEUE_STATE_OK;
            m_rendered = true;
        }

        if (pCurrentShader && shaderOk) SR_LIKELY_ATTRIBUTE {
            pCurrentShader->UnUse();
        }
    }

    void RenderQueue::Update(const SR_UTILS_NS::StringAtom& layer, RenderQueue::Queue& queue) {
        SR_TRACY_ZONE_S(layer.c_str());

        ShaderPtr pCurrentShader = nullptr;

        MeshInfo* pStart = queue.data();
        const MeshInfo* pEnd = pStart + queue.size();

        for (MeshInfo* pElement = pStart; pElement < pEnd; ++pElement) {
            const MeshInfo info = *pElement;

            if (SR_UTILS_NS::Math::IsMaskIncludedSubMask(info.state, QUEUE_STATE_ERROR)) SR_UNLIKELY_ATTRIBUTE {
                if (SR_UTILS_NS::Math::IsMaskIncludedSubMask(info.state, QUEUE_STATE_SHADER_ERROR)) {
                    pElement = FindNextShader(queue, pElement);
                }
                else if (SR_UTILS_NS::Math::IsMaskIncludedSubMask(info.state, QUEUE_STATE_VBO_ERROR)) {
                    pElement = FindNextVBO(queue, pElement);
                }
                continue;
            }

            if (info.shaderUseInfo.pShader != pCurrentShader) SR_UNLIKELY_ATTRIBUTE {
                pCurrentShader = info.shaderUseInfo.pShader;
                if (pCurrentShader->BeginSharedUBO()) SR_LIKELY_ATTRIBUTE {
                    m_meshDrawerPass->UseSharedUniforms(info.shaderUseInfo);
                    pElement->state |= MESH_STATE_SHADER_UPDATED;
                    pCurrentShader->EndSharedUBO();
                }
            }

            if (SR_UTILS_NS::Math::IsMaskIncludedSubMask(info.state, MESH_STATE_SHADER_UPDATED)) SR_LIKELY_ATTRIBUTE {
                pElement = FindNextShader(queue, pElement);
                continue;
            }

            if (SR_UTILS_NS::Math::IsMaskIncludedSubMask(info.state, MESH_STATE_VBO_UPDATED)) SR_LIKELY_ATTRIBUTE {
                pElement = FindNextVBO(queue, pElement);
                continue;
            }

            auto&& virtualUbo = info.pMesh->GetVirtualUBO();
            if (virtualUbo == SR_ID_INVALID) SR_UNLIKELY_ATTRIBUTE {
                continue;
            }

            m_meshDrawerPass->UseUniforms(info.shaderUseInfo, info.pMesh);
            pElement->state |= MESH_STATE_VBO_UPDATED | MESH_STATE_SHADER_UPDATED;

            if (m_uboManager.BindUBO(virtualUbo) == Memory::UBOManager::BindResult::Duplicated) SR_UNLIKELY_ATTRIBUTE {
                SR_ERROR("VBORenderStage::Update() : memory has been duplicated!");
                continue;
            }

            SR_MAYBE_UNUSED_VAR info.shaderUseInfo.pShader->Flush();
        }

        m_renderContext->SetCurrentShader(nullptr);
        m_renderScene->SetCurrentSkeleton(nullptr);
    }

    RenderQueue::MeshInfo* RenderQueue::FindNextShader(Queue& queue, MeshInfo* pElement) {
        SR_TRACY_ZONE;

        auto pEnd = queue.data() + queue.size();
        auto pShader = pElement->shaderUseInfo.pShader;

        while (pElement != pEnd) SR_UNLIKELY_ATTRIBUTE {
            if (pElement->shaderUseInfo.pShader != pShader) SR_UNLIKELY_ATTRIBUTE {
                return pElement;
            }
            ++pElement;
        }

        return pEnd;

        //const ShaderMismatchPredicate predicate(pElement->shaderUseInfo.pShader);
        //return queue.UpperBound(pElement, queue.data() + queue.size(), *pElement, predicate);
    }

    RenderQueue::MeshInfo* RenderQueue::FindNextVBO(Queue& queue, MeshInfo* pElement) {
        SR_TRACY_ZONE;

        auto pEnd = queue.data() + queue.size();
        auto vbo = pElement->vbo;

        while (pElement != pEnd) {
            if (pElement->vbo != vbo) {
                return pElement;
            }
            ++pElement;
        }

        return pEnd;

        //const ShaderVBOMismatchPredicate predicate(pElement->shaderUseInfo.pShader, pElement->vbo);
        //return queue.UpperBound(pElement, queue.data() + queue.size(), *pElement, predicate);
    }

    bool RenderQueue::UseShader(ShaderUseInfo info) {
        SR_TRACY_ZONE;

        auto pShader = info.pShader;

        if (pShader->Use() == ShaderBindResult::Failed) {
            return false;
        }

        m_renderContext->SetCurrentShader(pShader);

        if (!pShader->IsSamplersValid()) {
            std::string message = "Shader samplers is not valid!\n\tPath: " + pShader->GetResourcePath().ToStringRef();
            for (auto&& [name, sampler] : pShader->GetSamplers()) {
                if (m_pipeline->IsSamplerValid(sampler.samplerId)) {
                    continue;
                }

                message += "\n\tSampler is not set: " + name.ToStringRef();
            }
            m_renderStrategy->AddError(message);
            pShader->UnUse();
            return false;
        }

        if (m_pipeline->IsShaderChanged()) {
            m_meshDrawerPass->UseConstants(info);
            m_meshDrawerPass->UseSamplers(info);
        }

        return true;
    }

    void RenderQueue::PrepareLayers() {
        SR_TRACY_ZONE;

        auto&& layerManager = SR_UTILS_NS::LayerManager::Instance();

        if (layerManager.GetHashState() == m_layersStateHash) {
            return;
        }

        SR_MAYBE_UNUSED auto&& guard = SR_UTILS_NS::LayerManager::ScopeLockSingleton();

        m_layersStateHash = layerManager.GetHashState();

        auto stash = std::move(m_queues);

        for (auto&& layer : layerManager.GetLayers()) {
            if (!m_meshDrawerPass->IsLayerAllowed(layer)) {
                continue;
            }
            m_queues.emplace_back(layer, Queue());
        }

        for (auto&& [layer, queue] : stash) {
            for (auto&& [newLayer, newQueue] : m_queues) {
                if (layer == newLayer) {
                    newQueue = std::move(queue);
                    break;
                }
            }
        }
    }

    SR_GRAPH_NS::ShaderUseInfo RenderQueue::GetShaderUseInfo(const MeshRegistrationInfo& info) const {
        return SR_GRAPH_NS::ShaderUseInfo(info.pMaterial ? info.pMaterial->GetShader() : nullptr);
    }
}
