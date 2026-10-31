#pragma once

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <windows.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class VulkanGpuSelection
{
    Auto,
    Integrated,
    Discrete,
};

enum class VulkanFrameResult
{
    NotSubmitted,
    Submitted,
    Presented,
};

struct VulkanVertex
{
    float x;
    float y;
    float red;
    float green;
    float blue;
    float alpha;
};

class VulkanRenderer final
{
public:
    VulkanRenderer() = default;
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    ~VulkanRenderer();

    void Initialize(
        HWND window,
        std::uint32_t width,
        std::uint32_t height,
        VulkanGpuSelection gpuSelection,
        bool persistentCanvas);
    void SetVertices(const std::vector<VulkanVertex>& vertices);
    void SetCanvasVertices(const std::vector<VulkanVertex>& vertices);
    void SetClearColor(float red, float green, float blue);
    void Resize(std::uint32_t width, std::uint32_t height);
    VulkanFrameResult Render();
    void BeginCanvasRebuild();
    void RebuildCanvasBatch(
        const std::vector<VulkanVertex>& vertices,
        bool clearCanvas);
    void FinishCanvasRebuild();
    bool CanvasRebuildRequired() const noexcept
    {
        return canvasRebuildRequired_;
    }
    bool CanvasAvailable() const noexcept
    {
        return persistentCanvasEnabled_ && canvasImage_ != VK_NULL_HANDLE;
    }
    bool LastFrameCanvasGeometrySubmitted() const noexcept
    {
        return lastFrameCanvasGeometrySubmitted_;
    }
    void WaitIdle();
    void Shutdown() noexcept;

    const std::string& DeviceName() const noexcept { return deviceName_; }
    std::uint32_t VendorId() const noexcept { return vendorId_; }
    std::uint32_t DeviceId() const noexcept { return deviceId_; }

private:
    struct FrameResources
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkDeviceSize vertexCapacity = 0;
    };

    void CreateInstance();
    void CreateSurface();
    void SelectPhysicalDevice(VulkanGpuSelection gpuSelection);
    void CreateLogicalDevice();
    void CreateCommandPoolAndFrames();
    void RecreateSwapchain();
    bool CreateSwapchainResources(VkSwapchainKHR oldSwapchain);
    void CreateRenderPass();
    void CreateGraphicsPipeline(
        VkRenderPass targetRenderPass,
        VkPipelineLayout& targetLayout,
        VkPipeline& targetPipeline);
    void CreateCanvasResources();
    void CreateCanvasImage();
    void DestroyCanvasImage() noexcept;
    void CreateCanvasRenderPass();
    void CreateCanvasDescriptorResources();
    void CreateCompositePipeline();
    void RecordCanvasUpdate(
        VkCommandBuffer commandBuffer,
        const std::vector<VulkanVertex>& vertices,
        bool clearCanvas);
    void RecordCanvasComposite(
        VkCommandBuffer commandBuffer,
        VkFramebuffer framebuffer);
    void CreateFramebuffers();
    void CreateFrameVertexBuffer(FrameResources& frame, VkDeviceSize capacity);
    void UploadVertices(
        FrameResources& frame,
        const std::vector<VulkanVertex>& vertices);
    std::uint32_t FindMemoryType(
        std::uint32_t typeBits,
        VkMemoryPropertyFlags properties) const;
    void DestroySwapchainResources() noexcept;

    HWND window_ = nullptr;
    std::uint32_t requestedWidth_ = 0;
    std::uint32_t requestedHeight_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool initialized_ = false;
    bool persistentCanvasEnabled_ = false;
    bool resizePending_ = false;
    bool swapchainSuspended_ = false;
    bool canvasRebuildRequired_ = false;
    bool canvasClearPending_ = false;
    bool lastFrameCanvasGeometrySubmitted_ = false;
    std::uint32_t currentFrame_ = 0;
    std::array<float, 4> clearColor_ = {1.0f, 1.0f, 1.0f, 1.0f};
    std::string deviceName_;
    std::uint32_t vendorId_ = 0;
    std::uint32_t deviceId_ = 0;
    std::vector<VulkanVertex> vertices_;
    std::vector<VulkanVertex> pendingCanvasVertices_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    std::uint32_t queueFamilyIndex_ = 0;
    std::uint32_t presentQueueFamilyIndex_ = 0;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat pipelineFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkRenderPass canvasRenderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout canvasPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline canvasPipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout canvasDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool canvasDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet canvasDescriptorSet_ = VK_NULL_HANDLE;
    VkSampler canvasSampler_ = VK_NULL_HANDLE;
    VkImage canvasImage_ = VK_NULL_HANDLE;
    VkDeviceMemory canvasMemory_ = VK_NULL_HANDLE;
    VkImageView canvasImageView_ = VK_NULL_HANDLE;
    VkFramebuffer canvasFramebuffer_ = VK_NULL_HANDLE;
    VkFormat canvasFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent2D canvasExtent_{};
    VkImageLayout canvasImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineLayout compositePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> imageViews_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> renderFinished_;
    std::vector<VkFence> imagesInFlight_;
    FrameResources frames_[2];
};
