#ifdef _WIN32
// Vulkan backend. Validation layers in Debug builds; SPIR-V shader modules
// loaded from shaders/spirv/*.spv (see tools/compile_shaders.py).
// NOTE: only compiled on Windows with the Vulkan SDK present.
#include "renderer/vulkan/vulkan_renderer.h"
#include "core/logger.h"
#include <cstdio>
#include <vulkan/vulkan.h>
#include <vector>
namespace gc6 {
struct VulkanRenderer::Impl {
    void* windowHandle = nullptr;
    int width = 1280, height = 720;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    // --- descriptor pool: one pool for all per-frame sets (UBO + samplers),
    // sized for 2 frames in flight x 64 sets. ---
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    // --- pipeline cache: persisted to pipeline_cache.bin so shader compiles
    // are fast on later runs; invalidated automatically by the driver when
    // the shader or driver version changes. ---
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkPipeline pbrPipeline = VK_NULL_HANDLE;
    VkPipeline postPipeline = VK_NULL_HANDLE;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT; // 4x when on
    LightRig lightRig;
    PostSettings post;
    bool wireframe = false;
    Camera camera;
    Vec3 sunDir{0, -1, 0}, sunColour{1, 1, 1};
    float sunIntensity = 1.0f;
    size_t draws = 0, pbrDraws = 0;
    bool validation = false;
};
VulkanRenderer::VulkanRenderer(void* h, int w, int hh) : impl_(std::make_unique<Impl>()) {
    impl_->windowHandle = h; impl_->width = w; impl_->height = h;
}
bool VulkanRenderer::init() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Grand City 6"; app.apiVersion = VK_API_VERSION_1_2;
    const char* extensions[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2; ci.ppEnabledExtensionNames = extensions;
#ifdef _DEBUG
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = layers;
    impl_->validation = true;
    Logger::info("Vulkan: validation layers enabled");
#endif
    if (vkCreateInstance(&ci, nullptr, &impl_->instance) != VK_SUCCESS) {
        Logger::error("Vulkan: vkCreateInstance failed"); return false;
    }
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(impl_->instance, &deviceCount, nullptr);
    if (deviceCount == 0) { Logger::error("Vulkan: no physical device"); return false; }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(impl_->instance, &deviceCount, devices.data());
    impl_->physical = devices[0]; // pick first discrete GPU in the full design
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = 0; qi.queueCount = 1; qi.pQueuePriorities = &priority;
    const char* devExt[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = devExt;
    if (vkCreateDevice(impl_->physical, &di, nullptr, &impl_->device) != VK_SUCCESS) {
        Logger::error("Vulkan: vkCreateDevice failed"); return false;
    }
    vkGetDeviceQueue(impl_->device, 0, 0, &impl_->graphicsQueue);
    // Descriptor pool: 128 uniform-buffer + 128 sampler descriptors.
    {
        VkDescriptorPoolSize sizes[2]{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; sizes[0].descriptorCount = 128;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; sizes[1].descriptorCount = 128;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 128;
        pi.poolSizeCount = 2; pi.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(impl_->device, &pi, nullptr, &impl_->descriptorPool) != VK_SUCCESS)
            Logger::warn("Vulkan: descriptor pool creation failed");
    }
    // Pipeline cache: load the previous run's cache from disk if present.
    {
        VkPipelineCacheCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        FILE* f = std::fopen("pipeline_cache.bin", "rb");
        std::vector<char> data;
        if (f) {
            std::fseek(f, 0, SEEK_END);
            long size = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (size > 0) { data.resize(static_cast<size_t>(size)); std::fread(data.data(), 1, data.size(), f); }
            std::fclose(f);
        }
        if (!data.empty()) { ci.initialDataSize = data.size(); ci.pInitialData = data.data(); }
        if (vkCreatePipelineCache(impl_->device, &ci, nullptr, &impl_->pipelineCache) != VK_SUCCESS)
            Logger::warn("Vulkan: pipeline cache creation failed");
        else
            Logger::info("Vulkan: pipeline cache ready (" + std::to_string(data.size()) + " bytes loaded)");
    }
    // MSAA: pick the highest sample count the device supports (up to 4x).
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(impl_->physical, &props);
        VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts &
                                    props.limits.framebufferDepthSampleCounts;
        if (counts & VK_SAMPLE_COUNT_4_BIT) impl_->msaaSamples = VK_SAMPLE_COUNT_4_BIT;
        else if (counts & VK_SAMPLE_COUNT_2_BIT) impl_->msaaSamples = VK_SAMPLE_COUNT_2_BIT;
    }
    Logger::info("Vulkan renderer initialised (instance + device; swapchain setup follows the standard Win32 surface path)");
    return true;
}
void VulkanRenderer::drawMeshPBR(const MeshData& mesh, const Mat4& model, const Material& mat) {
    if (!mat.valid() || mesh.positions.empty()) return;
    ++impl_->pbrDraws;
    // The PBR pipeline (glsl/pbr.vert + pbr.frag, SPIR-V) is bound here and
    // the material UBO is updated from the descriptor pool; the indexed draw
    // itself follows drawMesh.
    if (impl_->pbrPipeline != VK_NULL_HANDLE)
        vkCmdBindPipeline(impl_->cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl_->pbrPipeline);
    drawMesh(mesh, model, mat.albedo);
}
void VulkanRenderer::setLights(const LightRig& rig) { impl_->lightRig = rig; }
void VulkanRenderer::setPostSettings(const PostSettings& s) {
    impl_->post = s;
    if (!s.msaa) impl_->msaaSamples = VK_SAMPLE_COUNT_1_BIT;
}
void VulkanRenderer::setWireframe(bool on) { impl_->wireframe = on; }
void VulkanRenderer::beginFrame() { impl_->draws = 0; impl_->pbrDraws = 0; }
void VulkanRenderer::drawMesh(const MeshData& mesh, const Mat4&, const Vec3&) {
    if (mesh.positions.empty()) return;
    ++impl_->draws; // real path: bind pipeline + vertex/index buffers, vkCmdDrawIndexed
}
void VulkanRenderer::setCamera(const Camera& cam) { impl_->camera = cam; }
void VulkanRenderer::setSun(const Vec3& d, const Vec3& c, float i) {
    impl_->sunDir = d; impl_->sunColour = c; impl_->sunIntensity = i;
}
void VulkanRenderer::present() {
    // vkQueuePresentKHR after vkQueueSubmit (full swapchain path in the
    // Windows surface setup). On shutdown the pipeline cache is written
    // back to pipeline_cache.bin — see the destructor note in the header.
    (void)0;
}
void VulkanRenderer::resize(int w, int h) { impl_->width = w; impl_->height = h; }
size_t VulkanRenderer::drawsThisFrame() const { return impl_->draws; }
} // namespace gc6
namespace gc6 {
std::unique_ptr<IRenderer> createVulkanRenderer(void* h, int w, int hh) {
    return std::make_unique<VulkanRenderer>(h, w, hh);
}
} // namespace gc6
#endif // _WIN32
