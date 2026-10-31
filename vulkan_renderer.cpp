#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "vulkan_renderer.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
constexpr std::uint32_t kFramesInFlight = 2;

const char* VkResultName(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "unknown VkResult";
    }
}

void CheckVk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        std::ostringstream message;
        message << operation << " failed (" << VkResultName(result)
                << ", VkResult=" << static_cast<int>(result) << ')';
        throw std::runtime_error(message.str());
    }
}

bool HasDeviceExtension(
    VkPhysicalDevice device,
    const char* extensionName)
{
    std::uint32_t count = 0;
    CheckVk(
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
        "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> extensions(count);
    CheckVk(
        vkEnumerateDeviceExtensionProperties(
            device, nullptr, &count, extensions.data()),
        "vkEnumerateDeviceExtensionProperties");
    return std::any_of(
        extensions.begin(), extensions.end(),
        [extensionName](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, extensionName) == 0;
        });
}

VkSurfaceFormatKHR ChooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats)
{
    const auto preferredFormat = std::find_if(
        formats.begin(), formats.end(),
        [](const VkSurfaceFormatKHR& format) {
            return format.format == VK_FORMAT_B8G8R8A8_UNORM &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    if (preferredFormat != formats.end())
    {
        return *preferredFormat;
    }

    const auto fallbackFormat = std::find_if(
        formats.begin(), formats.end(),
        [](const VkSurfaceFormatKHR& format) {
            return format.format == VK_FORMAT_R8G8B8A8_UNORM &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    if (fallbackFormat != formats.end())
    {
        return *fallbackFormat;
    }

    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED)
    {
        return {VK_FORMAT_B8G8R8A8_UNORM, formats.front().colorSpace};
    }
    return formats.front();
}

bool SupportsColorAttachmentFormat(
    VkPhysicalDevice device,
    VkFormat format)
{
    if (format == VK_FORMAT_UNDEFINED)
    {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    return (properties.optimalTilingFeatures &
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
}

bool SupportsCanvasFormat(VkPhysicalDevice device)
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(
        device, VK_FORMAT_R8G8B8A8_UNORM, &properties);
    constexpr VkFormatFeatureFlags requiredFeatures =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    return (properties.optimalTilingFeatures & requiredFeatures) ==
        requiredFeatures;
}

bool SupportsImmediatePresentMode(
    const std::vector<VkPresentModeKHR>& presentModes)
{
    return std::find(
               presentModes.begin(), presentModes.end(),
               VK_PRESENT_MODE_IMMEDIATE_KHR) != presentModes.end();
}

std::vector<std::uint32_t> ReadShaderResource(
    WORD resourceId,
    const char* resourceName)
{
    const HMODULE executableModule = GetModuleHandleW(nullptr);
    if (executableModule == nullptr)
    {
        throw std::runtime_error(
            "GetModuleHandleW failed while locating embedded Vulkan shaders");
    }

    const HRSRC resource = FindResourceW(
        executableModule, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (resource == nullptr)
    {
        throw std::runtime_error(
            std::string("Could not find embedded Vulkan SPIR-V resource: ") +
            resourceName);
    }

    const DWORD size = SizeofResource(executableModule, resource);
    if (size == 0 || size % sizeof(std::uint32_t) != 0)
    {
        throw std::runtime_error(
            std::string("Embedded Vulkan SPIR-V resource has an invalid byte length: ") +
            resourceName);
    }

    const HGLOBAL loadedResource = LoadResource(executableModule, resource);
    if (loadedResource == nullptr)
    {
        throw std::runtime_error(
            std::string("Could not load embedded Vulkan SPIR-V resource: ") +
            resourceName);
    }

    const void* resourceData = LockResource(loadedResource);
    if (resourceData == nullptr)
    {
        throw std::runtime_error(
            std::string("Could not access embedded Vulkan SPIR-V resource: ") +
            resourceName);
    }

    std::vector<std::uint32_t> code(
        static_cast<std::size_t>(size) / sizeof(std::uint32_t));
    std::memcpy(code.data(), resourceData, size);
    return code;
}
} // namespace

VulkanRenderer::~VulkanRenderer()
{
    Shutdown();
}

void VulkanRenderer::Initialize(
    HWND window,
    std::uint32_t width,
    std::uint32_t height,
    VulkanGpuSelection gpuSelection,
    bool persistentCanvas)
{
    if (window == nullptr)
    {
        throw std::runtime_error(
            "VulkanRenderer::Initialize requires a valid Win32 window");
    }
    window_ = window;
    requestedWidth_ = width;
    requestedHeight_ = height;
    persistentCanvasEnabled_ = persistentCanvas;

    try
    {
        CreateInstance();
        CreateSurface();
        SelectPhysicalDevice(gpuSelection);
        CreateLogicalDevice();
        CreateCommandPoolAndFrames();
        if (persistentCanvasEnabled_)
        {
            CreateCanvasResources();
        }
        RecreateSwapchain();
        initialized_ = true;
    }
    catch (...)
    {
        Shutdown();
        throw;
    }
}

void VulkanRenderer::SetVertices(const std::vector<VulkanVertex>& vertices)
{
    if (vertices.size() >
        static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max() / sizeof(VulkanVertex)))
    {
        throw std::overflow_error(
            "Vulkan vertex count exceeds the supported draw limit");
    }
    vertices_ = vertices;
}

void VulkanRenderer::SetCanvasVertices(
    const std::vector<VulkanVertex>& vertices)
{
    if (!persistentCanvasEnabled_)
    {
        throw std::logic_error(
            "Canvas geometry was submitted to a non-persistent Vulkan renderer");
    }
    const std::size_t maximumVertexCount =
        std::numeric_limits<std::uint32_t>::max() / sizeof(VulkanVertex);
    if (vertices.size() > maximumVertexCount ||
        pendingCanvasVertices_.size() > maximumVertexCount - vertices.size())
    {
        throw std::overflow_error(
            "Vulkan canvas batch exceeds the supported draw limit");
    }
    pendingCanvasVertices_.insert(
        pendingCanvasVertices_.end(), vertices.begin(), vertices.end());
}

void VulkanRenderer::SetClearColor(float red, float green, float blue)
{
    clearColor_ = {red, green, blue, 1.0f};
}

void VulkanRenderer::Resize(std::uint32_t width, std::uint32_t height)
{
    if (width != 0 && height != 0 &&
        width == requestedWidth_ && height == requestedHeight_ &&
        !swapchainSuspended_)
    {
        return;
    }
    requestedWidth_ = width;
    requestedHeight_ = height;
    if (width == 0 || height == 0)
    {
        swapchainSuspended_ = true;
        return;
    }
    swapchainSuspended_ = false;
    resizePending_ = true;
}

VulkanFrameResult VulkanRenderer::Render()
{
    lastFrameCanvasGeometrySubmitted_ = false;
    if (!initialized_ || swapchainSuspended_ ||
        requestedWidth_ == 0 || requestedHeight_ == 0)
    {
        return VulkanFrameResult::NotSubmitted;
    }
    if (resizePending_)
    {
        RecreateSwapchain();
    }
    if (canvasRebuildRequired_)
    {
        return VulkanFrameResult::NotSubmitted;
    }
    if (swapchain_ == VK_NULL_HANDLE || extent_.width == 0 ||
        extent_.height == 0 || framebuffers_.empty() ||
        renderPass_ == VK_NULL_HANDLE ||
        (persistentCanvasEnabled_
            ? compositePipeline_ == VK_NULL_HANDLE ||
                canvasImage_ == VK_NULL_HANDLE
            : pipeline_ == VK_NULL_HANDLE))
    {
        swapchainSuspended_ = true;
        resizePending_ = true;
        return VulkanFrameResult::NotSubmitted;
    }

    FrameResources& frame = frames_[currentFrame_];
    CheckVk(
        vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX),
        "vkWaitForFences(frame)");

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        device_,
        swapchain_,
        UINT64_MAX,
        frame.imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        resizePending_ = true;
        RecreateSwapchain();
        return VulkanFrameResult::NotSubmitted;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        CheckVk(acquireResult, "vkAcquireNextImageKHR");
    }
    const bool acquiredSuboptimal = acquireResult == VK_SUBOPTIMAL_KHR;

    if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE)
    {
        CheckVk(
            vkWaitForFences(
                device_, 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX),
            "vkWaitForFences(swapchain image)");
    }

    const std::vector<VulkanVertex>& frameVertices =
        persistentCanvasEnabled_ ? pendingCanvasVertices_ : vertices_;
    const bool updateCanvas = persistentCanvasEnabled_ &&
        (canvasClearPending_ || !pendingCanvasVertices_.empty());
    UploadVertices(frame, frameVertices);
    CheckVk(
        vkResetCommandBuffer(frame.commandBuffer, 0),
        "vkResetCommandBuffer");
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(
        vkBeginCommandBuffer(frame.commandBuffer, &beginInfo),
        "vkBeginCommandBuffer");

    if (persistentCanvasEnabled_)
    {
        if (updateCanvas)
        {
            RecordCanvasUpdate(
                frame.commandBuffer, pendingCanvasVertices_,
                canvasClearPending_);
        }
        RecordCanvasComposite(frame.commandBuffer, framebuffers_[imageIndex]);
    }
    else
    {
        VkClearValue clearValue{};
        std::copy(clearColor_.begin(), clearColor_.end(),
                  clearValue.color.float32);
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = framebuffers_[imageIndex];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = extent_;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(
            frame.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(
            frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

        VkViewport viewport{};
        viewport.width = static_cast<float>(extent_.width);
        viewport.height = static_cast<float>(extent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = extent_;
        vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);
        if (!vertices_.empty())
        {
            constexpr VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(
                frame.commandBuffer, 0, 1, &frame.vertexBuffer, &offset);
            vkCmdDraw(
                frame.commandBuffer,
                static_cast<std::uint32_t>(vertices_.size()),
                1,
                0,
                0);
        }
        vkCmdEndRenderPass(frame.commandBuffer);
    }
    CheckVk(
        vkEndCommandBuffer(frame.commandBuffer),
        "vkEndCommandBuffer");

    CheckVk(
        vkResetFences(device_, 1, &frame.inFlight),
        "vkResetFences(frame)");
    const VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_[imageIndex];
    CheckVk(
        vkQueueSubmit(queue_, 1, &submitInfo, frame.inFlight),
        "vkQueueSubmit");
    if (persistentCanvasEnabled_ && updateCanvas)
    {
        lastFrameCanvasGeometrySubmitted_ = !pendingCanvasVertices_.empty();
        pendingCanvasVertices_.clear();
        canvasClearPending_ = false;
    }
    imagesInFlight_[imageIndex] = frame.inFlight;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presentResult =
        vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentResult != VK_SUCCESS &&
        presentResult != VK_SUBOPTIMAL_KHR &&
        presentResult != VK_ERROR_OUT_OF_DATE_KHR)
    {
        CheckVk(presentResult, "vkQueuePresentKHR");
    }

    currentFrame_ = (currentFrame_ + 1U) % kFramesInFlight;
    if (acquiredSuboptimal ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        presentResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        resizePending_ = true;
    }
    return presentResult == VK_ERROR_OUT_OF_DATE_KHR
        ? VulkanFrameResult::Submitted
        : VulkanFrameResult::Presented;
}

void VulkanRenderer::BeginCanvasRebuild()
{
    if (!persistentCanvasEnabled_ || canvasImage_ == VK_NULL_HANDLE)
    {
        throw std::logic_error(
            "Cannot rebuild a persistent Vulkan canvas before it is created");
    }
    pendingCanvasVertices_.clear();
    canvasRebuildRequired_ = true;
    canvasClearPending_ = true;
}

void VulkanRenderer::RebuildCanvasBatch(
    const std::vector<VulkanVertex>& vertices,
    bool clearCanvas)
{
    if (!persistentCanvasEnabled_ || !canvasRebuildRequired_ ||
        canvasImage_ == VK_NULL_HANDLE || swapchainSuspended_)
    {
        throw std::logic_error(
            "Persistent Vulkan canvas rebuild is not currently available");
    }
    if (vertices.size() >
        static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max() / sizeof(VulkanVertex)))
    {
        throw std::overflow_error(
            "Vulkan canvas rebuild batch exceeds the supported draw limit");
    }

    FrameResources& frame = frames_[currentFrame_];
    CheckVk(
        vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX),
        "vkWaitForFences(canvas rebuild)");
    UploadVertices(frame, vertices);
    CheckVk(
        vkResetCommandBuffer(frame.commandBuffer, 0),
        "vkResetCommandBuffer(canvas rebuild)");
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(
        vkBeginCommandBuffer(frame.commandBuffer, &beginInfo),
        "vkBeginCommandBuffer(canvas rebuild)");
    RecordCanvasUpdate(frame.commandBuffer, vertices, clearCanvas);
    CheckVk(
        vkEndCommandBuffer(frame.commandBuffer),
        "vkEndCommandBuffer(canvas rebuild)");
    CheckVk(
        vkResetFences(device_, 1, &frame.inFlight),
        "vkResetFences(canvas rebuild)");
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    CheckVk(
        vkQueueSubmit(queue_, 1, &submitInfo, frame.inFlight),
        "vkQueueSubmit(canvas rebuild)");
    CheckVk(
        vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX),
        "vkWaitForFences(canvas rebuild completion)");
    canvasClearPending_ = false;
}

void VulkanRenderer::FinishCanvasRebuild()
{
    if (!persistentCanvasEnabled_ || !canvasRebuildRequired_ ||
        canvasImage_ == VK_NULL_HANDLE)
    {
        throw std::logic_error(
            "Persistent Vulkan canvas rebuild is not in progress");
    }
    canvasRebuildRequired_ = false;
}

void VulkanRenderer::WaitIdle()
{
    if (device_ != VK_NULL_HANDLE)
    {
        CheckVk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    }
}

void VulkanRenderer::Shutdown() noexcept
{
    if (device_ != VK_NULL_HANDLE)
    {
        const VkResult idleResult = vkDeviceWaitIdle(device_);
        if (idleResult != VK_SUCCESS)
        {
            std::cerr << "vkDeviceWaitIdle during Vulkan cleanup failed ("
                      << VkResultName(idleResult)
                      << ", VkResult=" << static_cast<int>(idleResult) << ")\n";
        }
        for (FrameResources& frame : frames_)
        {
            if (frame.vertexBuffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(device_, frame.vertexBuffer, nullptr);
                frame.vertexBuffer = VK_NULL_HANDLE;
            }
            if (frame.vertexMemory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device_, frame.vertexMemory, nullptr);
                frame.vertexMemory = VK_NULL_HANDLE;
            }
            if (frame.imageAvailable != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
                frame.imageAvailable = VK_NULL_HANDLE;
            }
            if (frame.inFlight != VK_NULL_HANDLE)
            {
                vkDestroyFence(device_, frame.inFlight, nullptr);
                frame.inFlight = VK_NULL_HANDLE;
            }
        }
        DestroySwapchainResources();
        DestroyCanvasImage();
        if (pipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        if (canvasPipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, canvasPipeline_, nullptr);
            canvasPipeline_ = VK_NULL_HANDLE;
        }
        if (canvasPipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, canvasPipelineLayout_, nullptr);
            canvasPipelineLayout_ = VK_NULL_HANDLE;
        }
        if (canvasRenderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device_, canvasRenderPass_, nullptr);
            canvasRenderPass_ = VK_NULL_HANDLE;
        }
        if (canvasSampler_ != VK_NULL_HANDLE)
        {
            vkDestroySampler(device_, canvasSampler_, nullptr);
            canvasSampler_ = VK_NULL_HANDLE;
        }
        if (compositePipelineLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(
                device_, compositePipelineLayout_, nullptr);
            compositePipelineLayout_ = VK_NULL_HANDLE;
        }
        if (canvasDescriptorSetLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(
                device_, canvasDescriptorSetLayout_, nullptr);
            canvasDescriptorSetLayout_ = VK_NULL_HANDLE;
        }
        if (swapchain_ != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
        if (commandPool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    physicalDevice_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
    initialized_ = false;
    persistentCanvasEnabled_ = false;
    canvasRebuildRequired_ = false;
    pendingCanvasVertices_.clear();
}

void VulkanRenderer::CreateInstance()
{
    std::uint32_t extensionCount = 0;
    CheckVk(
        vkEnumerateInstanceExtensionProperties(
            nullptr, &extensionCount, nullptr),
        "vkEnumerateInstanceExtensionProperties(count)");
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    CheckVk(
        vkEnumerateInstanceExtensionProperties(
            nullptr, &extensionCount, availableExtensions.data()),
        "vkEnumerateInstanceExtensionProperties");
    const auto hasInstanceExtension = [&availableExtensions](
                                          const char* name) {
        return std::any_of(
            availableExtensions.begin(), availableExtensions.end(),
            [name](const VkExtensionProperties& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
    };
    if (!hasInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME) ||
        !hasInstanceExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME))
    {
        throw std::runtime_error(
            "Vulkan loader is missing VK_KHR_surface or VK_KHR_win32_surface");
    }
    const std::array<const char*, 2> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "Native circle benchmark";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.pEngineName = "Circle benchmark Vulkan renderer";
    applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount =
        static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    CheckVk(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
}

void VulkanRenderer::CreateSurface()
{
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hwnd = window_;
    surfaceInfo.hinstance = GetModuleHandleW(nullptr);
    CheckVk(
        vkCreateWin32SurfaceKHR(
            instance_, &surfaceInfo, nullptr, &surface_),
        "vkCreateWin32SurfaceKHR");
}

void VulkanRenderer::SelectPhysicalDevice(
    VulkanGpuSelection gpuSelection)
{
    std::uint32_t deviceCount = 0;
    CheckVk(
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
        "vkEnumeratePhysicalDevices(count)");
    if (deviceCount == 0)
    {
        throw std::runtime_error(
            "Vulkan found no physical devices");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    CheckVk(
        vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()),
        "vkEnumeratePhysicalDevices");

    struct Candidate
    {
        VkPhysicalDevice device;
        VkPhysicalDeviceProperties properties;
        std::uint32_t graphicsQueueFamily;
        std::uint32_t presentQueueFamily;
        VkDeviceSize localMemory;
        int preference;
    };
    std::vector<Candidate> candidates;
    bool foundRequestedType = false;
    bool foundDeviceWithoutImmediatePresentMode = false;
    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);

        const bool matchingType =
            gpuSelection == VulkanGpuSelection::Auto ||
            (gpuSelection == VulkanGpuSelection::Integrated &&
             properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ||
            (gpuSelection == VulkanGpuSelection::Discrete &&
             properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
        if (!matchingType)
        {
            continue;
        }
        foundRequestedType = true;
        if (!HasDeviceExtension(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            continue;
        }

        VkSurfaceCapabilitiesKHR capabilities{};
        CheckVk(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                device, surface_, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if ((capabilities.supportedUsageFlags &
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0 ||
            capabilities.supportedCompositeAlpha == 0)
        {
            continue;
        }

        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(
            device, &queueCount, queueFamilies.data());
        std::uint32_t graphicsQueue = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t presentQueue = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t sharedQueue = std::numeric_limits<std::uint32_t>::max();
        for (std::uint32_t index = 0; index < queueCount; ++index)
        {
            if (queueFamilies[index].queueCount == 0)
            {
                continue;
            }
            VkBool32 presents = VK_FALSE;
            CheckVk(
                vkGetPhysicalDeviceSurfaceSupportKHR(
                    device, index, surface_, &presents),
                "vkGetPhysicalDeviceSurfaceSupportKHR");
            const bool supportsGraphics =
                (queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (supportsGraphics &&
                graphicsQueue == std::numeric_limits<std::uint32_t>::max())
            {
                graphicsQueue = index;
            }
            if (presents == VK_TRUE &&
                presentQueue == std::numeric_limits<std::uint32_t>::max())
            {
                presentQueue = index;
            }
            if (supportsGraphics && presents == VK_TRUE)
            {
                sharedQueue = index;
            }
        }
        if (sharedQueue != std::numeric_limits<std::uint32_t>::max())
        {
            graphicsQueue = sharedQueue;
            presentQueue = sharedQueue;
        }
        if (graphicsQueue == std::numeric_limits<std::uint32_t>::max() ||
            presentQueue == std::numeric_limits<std::uint32_t>::max())
        {
            continue;
        }
        std::uint32_t formatCount = 0;
        CheckVk(
            vkGetPhysicalDeviceSurfaceFormatsKHR(
                device, surface_, &formatCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        CheckVk(
            vkGetPhysicalDeviceSurfaceFormatsKHR(
                device, surface_, &formatCount, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (formats.empty())
        {
            continue;
        }
        const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
        if (!SupportsColorAttachmentFormat(device, surfaceFormat.format) ||
            (persistentCanvasEnabled_ && !SupportsCanvasFormat(device)))
        {
            continue;
        }

        std::uint32_t presentModeCount = 0;
        CheckVk(
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                device, surface_, &presentModeCount, nullptr),
            "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        CheckVk(
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                device, surface_, &presentModeCount, presentModes.data()),
            "vkGetPhysicalDeviceSurfacePresentModesKHR");
        if (!SupportsImmediatePresentMode(presentModes))
        {
            foundDeviceWithoutImmediatePresentMode = true;
            continue;
        }

        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(device, &memoryProperties);
        VkDeviceSize localMemory = 0;
        for (std::uint32_t index = 0;
             index < memoryProperties.memoryHeapCount;
             ++index)
        {
            const VkMemoryHeap& heap = memoryProperties.memoryHeaps[index];
            if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 &&
                localMemory <= std::numeric_limits<VkDeviceSize>::max() - heap.size)
            {
                localMemory += heap.size;
            }
        }
        int preference = 0;
        switch (properties.deviceType)
        {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: preference = 3; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: preference = 2; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: preference = 1; break;
        default: preference = 0; break;
        }
        candidates.push_back({
            device, properties, graphicsQueue, presentQueue,
            localMemory, preference});
    }

    if (candidates.empty())
    {
        if (foundDeviceWithoutImmediatePresentMode)
        {
            throw std::runtime_error(
                "No suitable Vulkan GPU exposes the required "
                "VK_PRESENT_MODE_IMMEDIATE_KHR presentation mode");
        }
        const char* requestedType =
            gpuSelection == VulkanGpuSelection::Integrated ? "integrated" :
            gpuSelection == VulkanGpuSelection::Discrete ? "discrete" :
                                                          "usable";
        throw std::runtime_error(
            std::string("No ") + requestedType +
            " Vulkan GPU supports graphics, Win32 presentation, and "
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
            " with color-attachment swapchain support" +
            (foundRequestedType ? "; matching devices lack required support"
                                : "; the requested GPU type is not available"));
    }
    const auto selected = std::max_element(
        candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            if (left.preference != right.preference)
            {
                return left.preference < right.preference;
            }
            return left.localMemory < right.localMemory;
        });
    physicalDevice_ = selected->device;
    queueFamilyIndex_ = selected->graphicsQueueFamily;
    presentQueueFamilyIndex_ = selected->presentQueueFamily;
    deviceName_ = selected->properties.deviceName;
    vendorId_ = selected->properties.vendorID;
    deviceId_ = selected->properties.deviceID;
}

void VulkanRenderer::CreateLogicalDevice()
{
    constexpr float queuePriority = 1.0f;
    std::array<VkDeviceQueueCreateInfo, 2> queueInfos{};
    queueInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfos[0].queueFamilyIndex = queueFamilyIndex_;
    queueInfos[0].queueCount = 1;
    queueInfos[0].pQueuePriorities = &queuePriority;
    std::uint32_t queueInfoCount = 1;
    if (presentQueueFamilyIndex_ != queueFamilyIndex_)
    {
        queueInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[1].queueFamilyIndex = presentQueueFamilyIndex_;
        queueInfos[1].queueCount = 1;
        queueInfos[1].pQueuePriorities = &queuePriority;
        queueInfoCount = 2;
    }
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = queueInfoCount;
    deviceInfo.pQueueCreateInfos = queueInfos.data();
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = extensions;
    CheckVk(
        vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_),
        "vkCreateDevice");
    vkGetDeviceQueue(device_, queueFamilyIndex_, 0, &queue_);
    vkGetDeviceQueue(device_, presentQueueFamilyIndex_, 0, &presentQueue_);
}

void VulkanRenderer::CreateCommandPoolAndFrames()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags =
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex_;
    CheckVk(
        vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
        "vkCreateCommandPool");

    std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
    VkCommandBufferAllocateInfo allocationInfo{};
    allocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocationInfo.commandPool = commandPool_;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = kFramesInFlight;
    CheckVk(
        vkAllocateCommandBuffers(
            device_, &allocationInfo, commandBuffers.data()),
        "vkAllocateCommandBuffers");
    for (std::uint32_t index = 0; index < kFramesInFlight; ++index)
    {
        FrameResources& frame = frames_[index];
        frame.commandBuffer = commandBuffers[index];
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        CheckVk(
            vkCreateSemaphore(
                device_, &semaphoreInfo, nullptr, &frame.imageAvailable),
            "vkCreateSemaphore(image available)");
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        CheckVk(
            vkCreateFence(device_, &fenceInfo, nullptr, &frame.inFlight),
            "vkCreateFence(frame)");
    }
}

void VulkanRenderer::RecreateSwapchain()
{
    if (requestedWidth_ == 0 || requestedHeight_ == 0)
    {
        swapchainSuspended_ = true;
        resizePending_ = true;
        return;
    }
    if (device_ != VK_NULL_HANDLE)
    {
        WaitIdle();
    }
    DestroySwapchainResources();
    const VkSwapchainKHR oldSwapchain = swapchain_;
    swapchain_ = VK_NULL_HANDLE;
    bool created = false;
    try
    {
        created = CreateSwapchainResources(oldSwapchain);
    }
    catch (...)
    {
        if (oldSwapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
        }
        throw;
    }
    if (!created)
    {
        if (oldSwapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
        }
        resizePending_ = true;
        swapchainSuspended_ = true;
        width_ = 0;
        height_ = 0;
        currentFrame_ = 0;
        return;
    }
    if (oldSwapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
    }
    resizePending_ = false;
    swapchainSuspended_ = false;
    width_ = extent_.width;
    height_ = extent_.height;
    currentFrame_ = 0;
}

bool VulkanRenderer::CreateSwapchainResources(VkSwapchainKHR oldSwapchain)
{
    VkSurfaceCapabilitiesKHR capabilities{};
    CheckVk(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physicalDevice_, surface_, &capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if ((capabilities.supportedUsageFlags &
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0)
    {
        throw std::runtime_error(
            "Vulkan surface does not support color-attachment swapchain images");
    }

    std::uint32_t formatCount = 0;
    CheckVk(
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            physicalDevice_, surface_, &formatCount, nullptr),
        "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    CheckVk(
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            physicalDevice_, surface_, &formatCount, formats.data()),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");
    if (formats.empty())
    {
        throw std::runtime_error(
            "Vulkan surface exposes no swapchain image formats");
    }
    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
    if (!SupportsColorAttachmentFormat(
            physicalDevice_, surfaceFormat.format))
    {
        throw std::runtime_error(
            "Selected Vulkan surface format does not support color attachments");
    }

    std::uint32_t presentModeCount = 0;
    CheckVk(
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            physicalDevice_, surface_, &presentModeCount, nullptr),
        "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    CheckVk(
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            physicalDevice_, surface_, &presentModeCount, presentModes.data()),
        "vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (presentModes.empty())
    {
        throw std::runtime_error(
            "Vulkan surface exposes no presentation modes");
    }
    const auto supportsPresentMode =
        [&presentModes](VkPresentModeKHR mode) {
            return std::find(
                presentModes.begin(), presentModes.end(), mode) !=
                presentModes.end();
        };
    const bool immediateSupported =
        supportsPresentMode(VK_PRESENT_MODE_IMMEDIATE_KHR);
    if (!immediateSupported)
    {
        throw std::runtime_error(
            "Vulkan surface does not support the required "
            "VK_PRESENT_MODE_IMMEDIATE_KHR presentation mode");
    }
    constexpr VkPresentModeKHR presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;

    VkExtent2D chosenExtent{};
    if (capabilities.currentExtent.width !=
        std::numeric_limits<std::uint32_t>::max())
    {
        chosenExtent = capabilities.currentExtent;
    }
    else
    {
        chosenExtent.width = std::clamp(
            requestedWidth_,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        chosenExtent.height = std::clamp(
            requestedHeight_,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);
    }
    if (chosenExtent.width == 0 || chosenExtent.height == 0)
    {
        extent_ = chosenExtent;
        resizePending_ = true;
        return false;
    }
    std::uint32_t imageCount = capabilities.minImageCount + 1U;
    if (capabilities.maxImageCount != 0)
    {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    VkCompositeAlphaFlagBitsKHR compositeAlpha =
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha & compositeAlpha) == 0)
    {
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> alphaOptions = {
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        };
        const auto supported = std::find_if(
            alphaOptions.begin(), alphaOptions.end(),
            [&capabilities](VkCompositeAlphaFlagBitsKHR option) {
                return (capabilities.supportedCompositeAlpha & option) != 0;
            });
        if (supported == alphaOptions.end())
        {
            throw std::runtime_error(
                "Vulkan surface exposes no supported composite-alpha mode");
        }
        compositeAlpha = *supported;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface_;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = chosenExtent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const std::array<std::uint32_t, 2> queueFamilyIndices = {
        queueFamilyIndex_, presentQueueFamilyIndex_};
    if (queueFamilyIndex_ != presentQueueFamilyIndex_)
    {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount =
            static_cast<std::uint32_t>(queueFamilyIndices.size());
        swapchainInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    }
    else
    {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = compositeAlpha;
    swapchainInfo.presentMode = presentMode;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = oldSwapchain;
    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    CheckVk(
        vkCreateSwapchainKHR(
            device_, &swapchainInfo, nullptr, &newSwapchain),
        "vkCreateSwapchainKHR");
    swapchain_ = newSwapchain;
    swapchainFormat_ = surfaceFormat.format;
    extent_ = chosenExtent;

    std::uint32_t swapImageCount = 0;
    CheckVk(
        vkGetSwapchainImagesKHR(
            device_, swapchain_, &swapImageCount, nullptr),
        "vkGetSwapchainImagesKHR(count)");
    swapchainImages_.resize(swapImageCount);
    CheckVk(
        vkGetSwapchainImagesKHR(
            device_, swapchain_, &swapImageCount, swapchainImages_.data()),
        "vkGetSwapchainImagesKHR");
    imageViews_.reserve(swapImageCount);
    for (VkImage image : swapchainImages_)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat_;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VkImageView imageView = VK_NULL_HANDLE;
        CheckVk(
            vkCreateImageView(
                device_, &viewInfo, nullptr, &imageView),
            "vkCreateImageView(swapchain)");
        imageViews_.push_back(imageView);
    }
    if (pipeline_ != VK_NULL_HANDLE &&
        pipelineFormat_ != swapchainFormat_)
    {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    CreateRenderPass();
    if (persistentCanvasEnabled_)
    {
        if (compositePipelineLayout_ == VK_NULL_HANDLE)
        {
            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &canvasDescriptorSetLayout_;
            CheckVk(
                vkCreatePipelineLayout(
                    device_, &layoutInfo, nullptr,
                    &compositePipelineLayout_),
                "vkCreatePipelineLayout(composite)");
        }
        if (compositePipeline_ == VK_NULL_HANDLE)
        {
            CreateCompositePipeline();
        }
        if (canvasImage_ == VK_NULL_HANDLE ||
            canvasExtent_.width != extent_.width ||
            canvasExtent_.height != extent_.height)
        {
            DestroyCanvasImage();
            CreateCanvasImage();
            canvasRebuildRequired_ = true;
            canvasClearPending_ = true;
        }
    }
    else if (pipeline_ == VK_NULL_HANDLE)
    {
        CreateGraphicsPipeline(
            renderPass_, pipelineLayout_, pipeline_);
        pipelineFormat_ = swapchainFormat_;
    }
    CreateFramebuffers();
    renderFinished_.resize(swapImageCount, VK_NULL_HANDLE);
    imagesInFlight_.resize(swapImageCount, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& semaphore : renderFinished_)
    {
        CheckVk(
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore),
            "vkCreateSemaphore(render finished)");
    }
    return true;
}

void VulkanRenderer::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    CheckVk(
        vkCreateRenderPass(
            device_, &renderPassInfo, nullptr, &renderPass_),
        "vkCreateRenderPass");
}

void VulkanRenderer::CreateGraphicsPipeline(
    VkRenderPass targetRenderPass,
    VkPipelineLayout& targetLayout,
    VkPipeline& targetPipeline)
{
    const std::vector<std::uint32_t> vertexCode =
        ReadShaderResource(
            IDR_VULKAN_CIRCLE_VERT_SPV, "vulkan_circle.vert.spv");
    const std::vector<std::uint32_t> fragmentCode =
        ReadShaderResource(
            IDR_VULKAN_CIRCLE_FRAG_SPV, "vulkan_circle.frag.spv");
    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = vertexCode.size() * sizeof(std::uint32_t);
    shaderInfo.pCode = vertexCode.data();
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    CheckVk(
        vkCreateShaderModule(
            device_, &shaderInfo, nullptr, &vertexModule),
        "vkCreateShaderModule(vertex)");

    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    try
    {
        shaderInfo.codeSize = fragmentCode.size() * sizeof(std::uint32_t);
        shaderInfo.pCode = fragmentCode.data();
        CheckVk(
            vkCreateShaderModule(
                device_, &shaderInfo, nullptr, &fragmentModule),
            "vkCreateShaderModule(fragment)");

        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertexModule, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, "main", nullptr},
        }};
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(VulkanVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        const std::array<VkVertexInputAttributeDescription, 2> attributes = {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT,
             static_cast<std::uint32_t>(offsetof(VulkanVertex, x))},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
             static_cast<std::uint32_t>(offsetof(VulkanVertex, red))},
        }};
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;
        const std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount =
            static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        CheckVk(
            vkCreatePipelineLayout(
                device_, &layoutInfo, nullptr, &targetLayout),
            "vkCreatePipelineLayout");
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType =
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount =
            static_cast<std::uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = targetLayout;
        pipelineInfo.renderPass = targetRenderPass;
        pipelineInfo.subpass = 0;
        CheckVk(
            vkCreateGraphicsPipelines(
                device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                &targetPipeline),
            "vkCreateGraphicsPipelines");
    }
    catch (...)
    {
        if (targetPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, targetPipeline, nullptr);
            targetPipeline = VK_NULL_HANDLE;
        }
        if (targetLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device_, targetLayout, nullptr);
            targetLayout = VK_NULL_HANDLE;
        }
        if (fragmentModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device_, fragmentModule, nullptr);
        }
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        throw;
    }
    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
}

void VulkanRenderer::CreateCanvasResources()
{
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(
        physicalDevice_, canvasFormat_, &formatProperties);
    constexpr VkFormatFeatureFlags requiredFeatures =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((formatProperties.optimalTilingFeatures & requiredFeatures) !=
        requiredFeatures)
    {
        throw std::runtime_error(
            "Selected Vulkan GPU cannot render and sample the persistent canvas format");
    }

    CreateCanvasRenderPass();
    CreateCanvasDescriptorResources();
    CreateGraphicsPipeline(
        canvasRenderPass_, canvasPipelineLayout_, canvasPipeline_);
}

void VulkanRenderer::CreateCanvasRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = canvasFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    CheckVk(
        vkCreateRenderPass(
            device_, &renderPassInfo, nullptr, &canvasRenderPass_),
        "vkCreateRenderPass(canvas)");
}

void VulkanRenderer::CreateCanvasDescriptorResources()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    CheckVk(
        vkCreateDescriptorSetLayout(
            device_, &layoutInfo, nullptr, &canvasDescriptorSetLayout_),
        "vkCreateDescriptorSetLayout(canvas)");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    CheckVk(
        vkCreateSampler(device_, &samplerInfo, nullptr, &canvasSampler_),
        "vkCreateSampler(canvas)");
}

void VulkanRenderer::CreateCanvasImage()
{
    if (extent_.width == 0 || extent_.height == 0)
    {
        throw std::runtime_error(
            "Cannot create a persistent Vulkan canvas with a zero extent");
    }
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = canvasFormat_;
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    try
    {
        CheckVk(
            vkCreateImage(device_, &imageInfo, nullptr, &canvasImage_),
            "vkCreateImage(canvas)");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, canvasImage_, &requirements);
        VkMemoryAllocateInfo allocationInfo{};
        allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CheckVk(
            vkAllocateMemory(
                device_, &allocationInfo, nullptr, &canvasMemory_),
            "vkAllocateMemory(canvas)");
        CheckVk(
            vkBindImageMemory(device_, canvasImage_, canvasMemory_, 0),
            "vkBindImageMemory(canvas)");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = canvasImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = canvasFormat_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        CheckVk(
            vkCreateImageView(
                device_, &viewInfo, nullptr, &canvasImageView_),
            "vkCreateImageView(canvas)");

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = canvasRenderPass_;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &canvasImageView_;
        framebufferInfo.width = extent_.width;
        framebufferInfo.height = extent_.height;
        framebufferInfo.layers = 1;
        CheckVk(
            vkCreateFramebuffer(
                device_, &framebufferInfo, nullptr, &canvasFramebuffer_),
            "vkCreateFramebuffer(canvas)");

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        CheckVk(
            vkCreateDescriptorPool(
                device_, &poolInfo, nullptr, &canvasDescriptorPool_),
            "vkCreateDescriptorPool(canvas)");

        VkDescriptorSetAllocateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setInfo.descriptorPool = canvasDescriptorPool_;
        setInfo.descriptorSetCount = 1;
        setInfo.pSetLayouts = &canvasDescriptorSetLayout_;
        CheckVk(
            vkAllocateDescriptorSets(
                device_, &setInfo, &canvasDescriptorSet_),
            "vkAllocateDescriptorSets(canvas)");
        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.sampler = canvasSampler_;
        imageDescriptor.imageView = canvasImageView_;
        imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = canvasDescriptorSet_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageDescriptor;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

        canvasExtent_ = extent_;
        canvasImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
        canvasRebuildRequired_ = true;
        canvasClearPending_ = true;
    }
    catch (...)
    {
        DestroyCanvasImage();
        throw;
    }
}

void VulkanRenderer::DestroyCanvasImage() noexcept
{
    if (device_ == VK_NULL_HANDLE)
    {
        return;
    }
    if (canvasDescriptorPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, canvasDescriptorPool_, nullptr);
        canvasDescriptorPool_ = VK_NULL_HANDLE;
        canvasDescriptorSet_ = VK_NULL_HANDLE;
    }
    if (canvasFramebuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(device_, canvasFramebuffer_, nullptr);
        canvasFramebuffer_ = VK_NULL_HANDLE;
    }
    if (canvasImageView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device_, canvasImageView_, nullptr);
        canvasImageView_ = VK_NULL_HANDLE;
    }
    if (canvasImage_ != VK_NULL_HANDLE)
    {
        vkDestroyImage(device_, canvasImage_, nullptr);
        canvasImage_ = VK_NULL_HANDLE;
    }
    if (canvasMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, canvasMemory_, nullptr);
        canvasMemory_ = VK_NULL_HANDLE;
    }
    canvasExtent_ = {};
    canvasImageLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void VulkanRenderer::CreateCompositePipeline()
{
    const std::vector<std::uint32_t> vertexCode =
        ReadShaderResource(
            IDR_VULKAN_CANVAS_VERT_SPV, "vulkan_canvas.vert.spv");
    const std::vector<std::uint32_t> fragmentCode =
        ReadShaderResource(
            IDR_VULKAN_CANVAS_FRAG_SPV, "vulkan_canvas.frag.spv");
    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = vertexCode.size() * sizeof(std::uint32_t);
    shaderInfo.pCode = vertexCode.data();
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    CheckVk(
        vkCreateShaderModule(
            device_, &shaderInfo, nullptr, &vertexModule),
        "vkCreateShaderModule(canvas vertex)");
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    try
    {
        shaderInfo.codeSize = fragmentCode.size() * sizeof(std::uint32_t);
        shaderInfo.pCode = fragmentCode.data();
        CheckVk(
            vkCreateShaderModule(
                device_, &shaderInfo, nullptr, &fragmentModule),
            "vkCreateShaderModule(canvas fragment)");
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {{
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertexModule, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentModule, "main", nullptr},
        }};
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType =
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType =
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType =
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType =
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;
        const std::array<VkDynamicState, 2> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType =
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount =
            static_cast<std::uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType =
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = compositePipelineLayout_;
        pipelineInfo.renderPass = renderPass_;
        CheckVk(
            vkCreateGraphicsPipelines(
                device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                &compositePipeline_),
            "vkCreateGraphicsPipelines(composite)");
    }
    catch (...)
    {
        if (compositePipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device_, compositePipeline_, nullptr);
            compositePipeline_ = VK_NULL_HANDLE;
        }
        if (fragmentModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device_, fragmentModule, nullptr);
        }
        vkDestroyShaderModule(device_, vertexModule, nullptr);
        throw;
    }
    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
}

void VulkanRenderer::RecordCanvasUpdate(
    VkCommandBuffer commandBuffer,
    const std::vector<VulkanVertex>& vertices,
    bool clearCanvas)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = canvasImage_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    if (clearCanvas)
    {
        barrier.oldLayout = canvasImageLayout_;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask =
            canvasImageLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                ? VK_ACCESS_SHADER_READ_BIT
                : 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            canvasImageLayout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
            1, &barrier);
        VkClearColorValue clearValue{};
        std::copy(clearColor_.begin(), clearColor_.end(),
                  clearValue.float32);
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.layerCount = 1;
        vkCmdClearColorImage(
            commandBuffer, canvasImage_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    else
    {
        barrier.oldLayout = canvasImageLayout_;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = canvasRenderPass_;
    renderPassInfo.framebuffer = canvasFramebuffer_;
    renderPassInfo.renderArea.extent = canvasExtent_;
    vkCmdBeginRenderPass(
        commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    if (!vertices.empty())
    {
        vkCmdBindPipeline(
            commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, canvasPipeline_);
        VkViewport viewport{};
        viewport.width = static_cast<float>(canvasExtent_.width);
        viewport.height = static_cast<float>(canvasExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = canvasExtent_;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        constexpr VkDeviceSize offset = 0;
        const VkBuffer vertexBuffer =
            frames_[currentFrame_].vertexBuffer;
        vkCmdBindVertexBuffers(
            commandBuffer, 0, 1, &vertexBuffer, &offset);
        vkCmdDraw(
            commandBuffer, static_cast<std::uint32_t>(vertices.size()),
            1, 0, 0);
    }
    vkCmdEndRenderPass(commandBuffer);
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    canvasImageLayout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VulkanRenderer::RecordCanvasComposite(
    VkCommandBuffer commandBuffer,
    VkFramebuffer framebuffer)
{
    VkClearValue clearValue{};
    std::copy(clearColor_.begin(), clearColor_.end(),
              clearValue.color.float32);
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.extent = extent_;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(
        commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(
        commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline_);
    vkCmdBindDescriptorSets(
        commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        compositePipelineLayout_, 0, 1, &canvasDescriptorSet_, 0, nullptr);
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = extent_;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
}

void VulkanRenderer::CreateFramebuffers()
{
    framebuffers_.reserve(imageViews_.size());
    for (VkImageView imageView : imageViews_)
    {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageView;
        framebufferInfo.width = extent_.width;
        framebufferInfo.height = extent_.height;
        framebufferInfo.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        CheckVk(
            vkCreateFramebuffer(
                device_, &framebufferInfo, nullptr, &framebuffer),
            "vkCreateFramebuffer");
        framebuffers_.push_back(framebuffer);
    }
}

void VulkanRenderer::CreateFrameVertexBuffer(
    FrameResources& frame,
    VkDeviceSize capacity)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = capacity;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer newBuffer = VK_NULL_HANDLE;
    VkDeviceMemory newMemory = VK_NULL_HANDLE;
    CheckVk(
        vkCreateBuffer(device_, &bufferInfo, nullptr, &newBuffer),
        "vkCreateBuffer(vertex)");
    try
    {
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, newBuffer, &requirements);
        VkMemoryAllocateInfo allocationInfo{};
        allocationInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocationInfo.allocationSize = requirements.size;
        allocationInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CheckVk(
            vkAllocateMemory(
                device_, &allocationInfo, nullptr, &newMemory),
            "vkAllocateMemory(vertex)");
        CheckVk(
            vkBindBufferMemory(device_, newBuffer, newMemory, 0),
            "vkBindBufferMemory(vertex)");
    }
    catch (...)
    {
        if (newMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_, newMemory, nullptr);
        }
        vkDestroyBuffer(device_, newBuffer, nullptr);
        throw;
    }
    if (frame.vertexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device_, frame.vertexBuffer, nullptr);
    }
    if (frame.vertexMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, frame.vertexMemory, nullptr);
    }
    frame.vertexBuffer = newBuffer;
    frame.vertexMemory = newMemory;
    frame.vertexCapacity = capacity;
}

void VulkanRenderer::UploadVertices(
    FrameResources& frame,
    const std::vector<VulkanVertex>& vertices)
{
    if (vertices.empty())
    {
        return;
    }
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(vertices.size() * sizeof(VulkanVertex));
    if (bytes > frame.vertexCapacity)
    {
        VkDeviceSize capacity = std::max<VkDeviceSize>(4096, frame.vertexCapacity);
        while (capacity < bytes)
        {
            if (capacity > std::numeric_limits<VkDeviceSize>::max() / 2)
            {
                capacity = bytes;
                break;
            }
            capacity *= 2;
        }
        CreateFrameVertexBuffer(frame, capacity);
    }
    void* mapped = nullptr;
    CheckVk(
        vkMapMemory(device_, frame.vertexMemory, 0, bytes, 0, &mapped),
        "vkMapMemory(vertex)");
    std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, frame.vertexMemory);
}

std::uint32_t VulkanRenderer::FindMemoryType(
    std::uint32_t typeBits,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (std::uint32_t index = 0;
         index < memoryProperties.memoryTypeCount;
         ++index)
    {
        if ((typeBits & (1U << index)) != 0 &&
            (memoryProperties.memoryTypes[index].propertyFlags & properties) ==
                properties)
        {
            return index;
        }
    }
    throw std::runtime_error(
        "No Vulkan memory type satisfies the requested allocation properties");
}

void VulkanRenderer::DestroySwapchainResources() noexcept
{
    if (device_ == VK_NULL_HANDLE)
    {
        return;
    }
    for (VkSemaphore semaphore : renderFinished_)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
    }
    renderFinished_.clear();
    imagesInFlight_.clear();
    for (VkFramebuffer framebuffer : framebuffers_)
    {
        if (framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
    }
    framebuffers_.clear();
    if (compositePipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device_, compositePipeline_, nullptr);
        compositePipeline_ = VK_NULL_HANDLE;
    }
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    for (VkImageView imageView : imageViews_)
    {
        if (imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_, imageView, nullptr);
        }
    }
    imageViews_.clear();
    swapchainImages_.clear();
}
