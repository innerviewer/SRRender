//
// Created by Monika on 15.09.2023.
//

#ifndef SR_ENGINE_GRAPHICS_VULKAN_PIPELINE_H
#define SR_ENGINE_GRAPHICS_VULKAN_PIPELINE_H

#include <Graphics/Pipeline/Pipeline.h>

namespace EvoVulkan::Core {
    class VulkanKernel;
}

namespace SR_GRAPH_NS {
    class VulkanPipeline : public Pipeline {
        using Super = Pipeline;
    public:
        explicit VulkanPipeline(const RenderContextPtr& pContext)
            : Super(pContext)
        { }

        ~VulkanPipeline() override;

    public:
        bool InitOverlay() override;

        bool PreInit(const PipelinePreInitInfo& info) override;
        bool Init() override;

        bool Destroy() override;

    public:
        SR_NODISCARD PipelineType GetType() const noexcept override { return PipelineType::Vulkan; }

        SR_NODISCARD std::string GetVendor() const override;
        SR_NODISCARD std::string GetRenderer() const override { return "Vulkan"; }
        SR_NODISCARD std::string GetVersion() const override { return "VK_API_VERSION_1_3"; }

        SR_NODISCARD void* GetCurrentFBOHandle() const override;
        SR_NODISCARD std::set<void*> GetFBOHandles() const override;
        SR_NODISCARD uint8_t GetFrameBufferSampleCount() const override;
        SR_NODISCARD uint8_t GetBuildIterationsCount() const noexcept override;

        SR_NODISCARD EvoVulkan::Core::VulkanKernel* GetKernel() const noexcept { return m_kernel; }
        SR_NODISCARD uint64_t GetUsedMemory() const override;

        SR_NODISCARD int32_t AllocateUBO(uint32_t uboSize) override;
        SR_NODISCARD int32_t AllocDescriptorSet(const std::vector<uint64_t>& types) override;
        SR_NODISCARD int32_t AllocateShaderProgram(const SRShaderCreateInfo& createInfo, int32_t fbo) override;
        SR_NODISCARD int32_t AllocateTexture(const SRTextureCreateInfo& createInfo) override;

        void SetViewport(int32_t width, int32_t height) override;
        void SetScissor(int32_t width, int32_t height) override;

        void UpdateDescriptorSets(uint32_t descriptorSet, const SRDescriptorUpdateInfos& updateInfo) override;
        void UpdateUBO(uint32_t UBO, void* pData, uint64_t size) override;

        void UseShader(uint32_t shaderProgram) override;
        void UnUseShader() override;

        void BindFrameBuffer(FramebufferPtr pFBO) override;

        void ResetDescriptorSet() override;

    private:
        VkViewport m_viewport = { };
        VkRect2D m_scissor = { };
        VkRenderPassBeginInfo m_renderPassBI = { };
        VkCommandBufferBeginInfo m_cmdBufInfo = { };

        VkDescriptorSet m_currentDescriptorSets = VK_NULL_HANDLE;
        VkCommandBuffer m_currentCmd  = VK_NULL_HANDLE;
        VkPipelineLayout m_currentLayout = VK_NULL_HANDLE;

        std::vector<VkClearValue> m_clearValues;

        EvoVulkan::Complexes::FrameBuffer* m_currentVkFrameBuffer = nullptr;
        EvoVulkan::Complexes::Shader* m_currentVkShader = nullptr;
        EvoVulkan::Core::VulkanKernel* m_kernel = nullptr;

        VulkanTools::MemoryManager* m_memory = nullptr;

    };
}

#endif //SR_ENGINE_GRAPHICS_VULKAN_PIPELINE_H
